#include "archive.h"
#include "library.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#include <zlib.h>
namespace SwitchApp {
namespace {
constexpr uint64_t Limit = 1024ull * 1024 * 1024;
uint16_t u16(const unsigned char *p) { return p[0] | p[1] << 8; }
uint32_t u32(const unsigned char *p) {
  return u16(p) | (uint32_t)u16(p + 2) << 16;
}
bool readAt(FILE *f, uint64_t offset, void *data, size_t n) {
  return offset <= 512ull * 1024 * 1024 &&
         fseek(f, (long)offset, SEEK_SET) == 0 && fread(data, 1, n, f) == n;
}
bool safePath(const std::string &n) {
  if (n.empty() || n.size() > 1024 || n[0] == '/' ||
      n.find('\\') != std::string::npos || n.find(':') != std::string::npos)
    return false;
  size_t begin = 0;
  while (begin < n.size()) {
    auto end = n.find('/', begin);
    if (end == std::string::npos)
      end = n.size();
    auto s = n.substr(begin, end - begin);
    if (s.empty() || s == "." || s == "..")
      return false;
    for (unsigned char c : s)
      if (c < 32 || c == 127)
        return false;
    begin = end + 1;
  }
  return true;
}
// Called exclusively on a freshly-created, owned staging directory. lstat
// prevents following links even if another process changes a scratch entry.
void removeTree(const std::string &p) {
  struct stat s;
  if (lstat(p.c_str(), &s))
    return;
  if (!S_ISDIR(s.st_mode)) {
    remove(p.c_str());
    return;
  }
  DIR *d = opendir(p.c_str());
  if (!d)
    return;
  while (auto *e = readdir(d))
    if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
      removeTree(p + "/" + e->d_name);
  closedir(d);
  rmdir(p.c_str());
}
struct Entry {
  std::string name;
  uint32_t offset, compressed, size, crc;
  uint16_t method, flags;
};
bool extract(FILE *zip, uint64_t archiveSize, const Entry &e,
             const std::string &target, std::atomic<bool> &cancel) {
  unsigned char header[30];
  if (!readAt(zip, e.offset, header, 30) || u32(header) != 0x04034b50 ||
      u16(header + 6) != e.flags || u16(header + 8) != e.method)
    return false;
  const uint16_t nameSize = u16(header + 26), extra = u16(header + 28);
  std::string name(nameSize, '\0');
  if (!readAt(zip, e.offset + 30, name.data(), name.size()) || name != e.name)
    return false;
  uint64_t pos = (uint64_t)e.offset + 30 + nameSize + extra;
  if (pos + e.compressed > archiveSize)
    return false;
  if (!(e.flags & 8) &&
      (u32(header + 14) != e.crc || u32(header + 18) != e.compressed ||
       u32(header + 22) != e.size))
    return false;
  if (e.name.back() == '/')
    return e.size == 0 && mkdirs(target);
  auto slash = target.find_last_of('/');
  if (!mkdirs(target.substr(0, slash)))
    return false;
  FILE *out = fopen(target.c_str(), "wb");
  if (!out)
    return false;
  unsigned char in[32768], buffer[32768];
  uint64_t remaining = e.compressed, written = 0;
  uLong crc = crc32(0, nullptr, 0);
  z_stream z{};
  bool ok = true, ended = false;
  if (e.method == 8 && inflateInit2(&z, -MAX_WBITS) != Z_OK) {
    fclose(out);
    return false;
  }
  while (ok && remaining && !cancel) {
    size_t n = (size_t)std::min<uint64_t>(remaining, sizeof(in));
    if (!readAt(zip, pos, in, n)) {
      ok = false;
      break;
    }
    pos += n;
    remaining -= n;
    if (e.method == 0) {
      if (written + n > e.size || fwrite(in, 1, n, out) != n) {
        ok = false;
        break;
      }
      crc = crc32(crc, in, n);
      written += n;
    } else {
      z.next_in = in;
      z.avail_in = n;
      do {
        z.next_out = buffer;
        z.avail_out = sizeof(buffer);
        int ret = inflate(&z, Z_NO_FLUSH);
        size_t produced = sizeof(buffer) - z.avail_out;
        if (written + produced > e.size ||
            fwrite(buffer, 1, produced, out) != produced) {
          ok = false;
          break;
        }
        crc = crc32(crc, buffer, produced);
        written += produced;
        if (ret == Z_STREAM_END) {
          ended = true;
          if (remaining || z.avail_in)
            ok = false;
          break;
        }
        if (ret != Z_OK || (!produced && !z.avail_in)) {
          if (ret != Z_OK)
            ok = false;
          break;
        }
      } while (z.avail_in || z.avail_out == 0);
    }
  }
  if (e.method == 8)
    inflateEnd(&z);
  if (fclose(out))
    ok = false;
  return ok && !cancel && written == e.size && crc == e.crc &&
         (e.method == 0 || ended);
}
} // namespace
void recoverInstalls(const std::string &library) {
  DIR *d = opendir(library.c_str());
  if (!d)
    return;
  while (auto *e = readdir(d)) {
    std::string n = e->d_name;
    if (n.compare(0, 9, ".install-"))
      continue;
    auto separator = n.find('-', 9);
    if (separator == std::string::npos || separator == 9 ||
        separator == n.size() - 1)
      continue;
    bool owned = true;
    for (size_t i = 9; i < n.size(); i++)
      if (i != separator && (n[i] < '0' || n[i] > '9'))
        owned = false;
    if (owned)
      removeTree(library + "/" + n);
  }
  closedir(d);
}
std::string installArchive(const std::string &archive,
                           const std::string &library, int id,
                           std::atomic<bool> &cancel, std::string &error) {
  FILE *zip = fopen(archive.c_str(), "rb");
  if (!zip) {
    error = "Cannot read downloaded package";
    return {};
  }
  auto fail = [&](const char *s) {
    error = cancel ? "Cancelled" : s;
    fclose(zip);
    return std::string();
  };
  if (fseek(zip, 0, SEEK_END))
    return fail("Invalid ZIP");
  long size = ftell(zip);
  if (size < 22 || size > 512ll * 1024 * 1024)
    return fail("ZIP size is invalid");
  const auto tailSize = (size_t)std::min<long>(size, 65557);
  std::vector<unsigned char> tail(tailSize);
  if (!readAt(zip, size - tailSize, tail.data(), tailSize))
    return fail("Cannot read ZIP directory");
  int eocd = -1;
  for (int i = (int)tailSize - 22; i >= 0; i--)
    if (u32(tail.data() + i) == 0x06054b50 &&
        i + 22 + u16(tail.data() + i + 20) == tailSize) {
      eocd = i;
      break;
    }
  if (eocd < 0)
    return fail("ZIP directory missing");
  auto *end = tail.data() + eocd;
  uint16_t count = u16(end + 10);
  uint32_t directory = u32(end + 16), dirSize = u32(end + 12);
  if (u16(end + 4) || u16(end + 6) || u16(end + 8) != count || !count ||
      count > 8192 ||
      (uint64_t)directory + dirSize > (uint64_t)(size - tailSize + eocd))
    return fail("Multi-volume / ZIP64 / oversized ZIP is unsupported");
  std::vector<Entry> entries;
  std::set<std::string> names;
  uint64_t total = 0, pos = directory;
  std::string bundle;
  int bundles = 0;
  for (int i = 0; i < count; i++) {
    unsigned char h[46];
    if (!readAt(zip, pos, h, 46) || u32(h) != 0x02014b50)
      return fail("Invalid ZIP entry");
    Entry e;
    e.flags = u16(h + 8);
    e.method = u16(h + 10);
    e.crc = u32(h + 16);
    e.compressed = u32(h + 20);
    e.size = u32(h + 24);
    e.offset = u32(h + 42);
    auto n = u16(h + 28);
    e.name.resize(n);
    if (!n || !readAt(zip, pos + 46, e.name.data(), n))
      return fail("Invalid ZIP path");
    auto mode = u32(h + 38) >> 16;
    auto type = mode & 0170000;
    if (!safePath(e.name) || e.flags & (1 | 64 | 8192) ||
        (e.method != 0 && e.method != 8) ||
        (type && type != 0100000 && type != 0040000) || u16(h + 34) ||
        e.offset >= directory || e.compressed == UINT32_MAX ||
        e.size == UINT32_MAX)
      return fail("Unsafe or unsupported ZIP entry");
    std::string folded = e.name;
    std::transform(
        folded.begin(), folded.end(), folded.begin(),
        [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; });
    if (folded.back() == '/')
      folded.pop_back();
    if (!names.insert(folded).second)
      return fail("Duplicate ZIP path");
    total += e.size;
    if (total > Limit || e.size > 256u * 1024 * 1024)
      return fail("Expanded package exceeds limits");
    auto slash = e.name.find_last_of('/');
    auto leaf = slash == std::string::npos ? e.name : e.name.substr(slash + 1);
    if (leaf == "pdxinfo") {
      bundle = slash == std::string::npos ? "" : e.name.substr(0, slash + 1);
      bundles++;
    }
    entries.push_back(e);
    pos += 46 + n + u16(h + 30) + u16(h + 32);
    if (pos > (uint64_t)directory + dirSize)
      return fail("ZIP directory truncated");
  }
  if (pos != (uint64_t)directory + dirSize || bundles != 1)
    return fail("Package must contain exactly one PDX bundle");
  if (!mkdirs(library))
    return fail("Cannot create game library");
  std::string token =
      std::to_string(id) + "-" + std::to_string(SDL_GetPerformanceCounter());
  std::string stage = library + "/.install-" + token;
  if (mkdir(stage.c_str(), 0755))
    return fail("Cannot create install staging directory");
  bool ok = true;
  for (auto &e : entries) {
    if (cancel) {
      ok = false;
      break;
    }
    if (e.name.compare(0, bundle.size(), bundle) || e.name == bundle)
      continue;
    auto rel = e.name.substr(bundle.size());
    if (!extract(zip, directory, e, stage + "/" + rel, cancel)) {
      ok = false;
      break;
    }
  }
  fclose(zip);
  if (ok) {
    auto game = inspectGame(stage);
    if (!game.supported) {
      error = game.error;
      ok = false;
    }
  }
  std::string dest = library + "/pd-" + token + ".pdx";
  if (ok && !cancel && rename(stage.c_str(), dest.c_str()) == 0)
    return dest;
  if (error.empty())
    error =
        cancel ? "Cancelled" : "Package validation failed, or SD card is full";
  removeTree(stage);
  return {};
}
} // namespace SwitchApp
