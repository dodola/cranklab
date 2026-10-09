#include "library.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
namespace SwitchApp {
static bool isFile(const std::string &p) {
  struct stat s;
  return stat(p.c_str(), &s) == 0 && S_ISREG(s.st_mode);
}
static bool isDir(const std::string &p) {
  struct stat s;
  return stat(p.c_str(), &s) == 0 && S_ISDIR(s.st_mode);
}
bool mkdirs(const std::string &path) {
  if (path.empty() || path.size() > 1900)
    return false;
  for (size_t i = 1; i < path.size(); i++)
    if (path[i] == '/' && path[i - 1] != ':') {
      auto part = path.substr(0, i);
      if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST)
        return false;
    }
  return (mkdir(path.c_str(), 0755) == 0 || errno == EEXIST) && isDir(path);
}
std::string readText(const std::string &p, size_t limit) {
  FILE *f = fopen(p.c_str(), "rb");
  // A FAT rename cannot replace an existing file on libnx. If interrupted
  // between the two renames, the last committed record remains in .bak.
  if (!f)
    f = fopen((p + ".bak").c_str(), "rb");
  if (!f)
    return {};
  std::string result;
  char buffer[4096];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), f)) != 0) {
    if (n > limit - result.size()) {
      result.clear();
      break;
    }
    result.append(buffer, n);
  }
  if (ferror(f))
    result.clear();
  fclose(f);
  return result;
}
bool writeAtomic(const std::string &p, const std::string &text) {
  std::string part = p + ".tmp";
  FILE *f = fopen(part.c_str(), "wb");
  if (!f)
    return false;
  bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
  if (fflush(f) != 0 || fsync(fileno(f)) != 0)
    ok = false;
  if (fclose(f) != 0)
    ok = false;
  if (ok) {
#ifdef __SWITCH__
    std::string backup = p + ".bak";
    struct stat st;
    bool exists = stat(p.c_str(), &st) == 0;
    if (exists) {
      if (remove(backup.c_str()) != 0 && errno != ENOENT)
        ok = false;
      if (ok && rename(p.c_str(), backup.c_str()) != 0)
        ok = false;
    }
    if (ok && rename(part.c_str(), p.c_str()) != 0) {
      ok = false;
      if (exists)
        rename(backup.c_str(), p.c_str());
    }
    if (ok)
      remove(backup.c_str());
#else
    ok = rename(part.c_str(), p.c_str()) == 0;
#endif
  }
  if (!ok)
    remove(part.c_str());
  return ok;
}
Game inspectGame(const std::string &p) {
  Game g;
  g.path = p;
  g.name = p.substr(p.find_last_of('/') + 1);
  std::istringstream lines(readText(p + "/pdxinfo", 65536));
  std::string line;
  bool metadata = false;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    size_t eq = line.find('=');
    if (eq == std::string::npos)
      continue;
    auto key = line.substr(0, eq), value = line.substr(eq + 1);
    if (key == "name") {
      g.name = value;
      metadata = true;
    }
    if (key == "author")
      g.author = value;
    if (key == "bundleID")
      g.bundle = value;
  }
  struct stat s;
  const int nativeResult = stat((p + "/pdex.bin").c_str(), &s);
  const int nativeError = errno;
  if (!isDir(p) || !metadata)
    g.error = "Missing or invalid pdxinfo";
  else if (nativeResult == 0 && (!S_ISREG(s.st_mode) || s.st_size > 0))
    g.error = "C / Lua+C game: unsupported in this version";
  else if (nativeResult && nativeError != ENOENT)
    g.error = "Cannot inspect native game entry";
  else if (!isFile(p + "/main.pdz") && !isFile(p + "/main.lua"))
    g.error = "No Lua entry point";
  else
    g.supported = true;
  const std::string identity =
      g.bundle.empty() ? g.name + "\n" + g.author : g.bundle;
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char c : identity) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  char key[24];
  snprintf(key, sizeof(key), "%016llx", (unsigned long long)hash);
  g.saveKey = key;
  return g;
}
std::vector<Game> scanLibrary(const std::string &root) {
  std::vector<Game> result;
  DIR *d = opendir(root.c_str());
  if (!d)
    return result;
  while (auto *e = readdir(d)) {
    if (e->d_name[0] == '.')
      continue;
    auto p = root + "/" + e->d_name;
    if (isDir(p) && isFile(p + "/pdxinfo"))
      result.push_back(inspectGame(p));
  }
  closedir(d);
  std::sort(result.begin(), result.end(),
            [](const Game &a, const Game &b) { return a.name < b.name; });
  return result;
}
} // namespace SwitchApp
