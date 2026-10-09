#include "launcher.h"
#include "archive.h"
#include "json.h"
#include "library.h"
#include <curl/curl.h>
#include <regex>
#include <stdexcept>
namespace SwitchApp {
namespace {
constexpr uint64_t PackageLimit = 512ull * 1024 * 1024;
std::string match(const std::string &s, const std::string &expression) {
  std::smatch m;
  return std::regex_search(s, m, std::regex(expression)) ? m[1].str()
                                                         : std::string();
}
bool webUrl(const std::string &s) {
  return s.size() < 8192 && s.compare(0, 8, "https://") == 0 &&
         s.find_first_of("\r\n") == std::string::npos;
}
bool itchUrl(const std::string &s) {
  return !match(s, "^(https://[a-zA-Z0-9-]+\\.itch\\.io/[a-zA-Z0-9_-]+/?)$")
              .empty();
}
struct Transfer {
  std::atomic<bool> &cancel;
  std::atomic<uint64_t> &received;
  std::atomic<uint64_t> &total;
  std::string body;
  FILE *file = nullptr;
  uint64_t limit;
  static size_t write(char *p, size_t n, size_t count, void *userdata) {
    auto &t = *(Transfer *)userdata;
    if (count && n > SIZE_MAX / count)
      return 0;
    size_t len = n * count;
    auto soFar = t.received.load();
    if (t.cancel || len > t.limit - soFar)
      return 0;
    if (t.file) {
      if (fwrite(p, 1, len, t.file) != len)
        return 0;
    } else
      t.body.append(p, len);
    t.received = soFar + len;
    return len;
  }
  static int progress(void *userdata, curl_off_t total, curl_off_t received,
                      curl_off_t, curl_off_t) {
    auto &t = *(Transfer *)userdata;
    (void)received;
    t.total = total > 0 ? (uint64_t)total : 0;
    return t.cancel || (total > 0 && (uint64_t)total > t.limit);
  }
};
class Client {
  CURL *c = nullptr;
  std::string ca;
  std::atomic<bool> &cancel;
  std::atomic<uint64_t> &received;
  std::atomic<uint64_t> &total;

public:
  Client(const std::string &ca, std::atomic<bool> &cancel,
         std::atomic<uint64_t> &received, std::atomic<uint64_t> &total)
      : ca(ca), cancel(cancel), received(received), total(total) {
    c = curl_easy_init();
    if (!c)
      throw std::runtime_error("Cannot start network client");
    curl_easy_setopt(c, CURLOPT_COOKIEFILE, "");
  }
  ~Client() {
    if (c)
      curl_easy_cleanup(c);
  }
  std::string escape(const std::string &s) {
    char *p = curl_easy_escape(c, s.c_str(), (int)s.size());
    if (!p)
      throw std::runtime_error("Cannot prepare download request");
    std::string r = p;
    curl_free(p);
    return r;
  }
  std::string request(const std::string &url, const std::string &body = "",
                      const std::string &referer = "",
                      const std::string &file = "") {
    if (!webUrl(url))
      throw std::runtime_error("Publisher returned an invalid HTTPS URL");
    received = 0;
    total = 0;
    Transfer t{cancel,  received,
               total,   {},
               nullptr, file.empty() ? 4ull * 1024 * 1024 : PackageLimit};
    if (!file.empty()) {
      t.file = fopen(file.c_str(), "wb");
      if (!t.file)
        throw std::runtime_error("Cannot write download; check SD card space");
    }
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, "PlaydateSwitch/0.1.0");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, file.empty() ? 30L : 600L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_CAINFO, ca.c_str());
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING,
                     file.empty() ? "" : "identity");
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, Transfer::write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, Transfer::progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &t);
    curl_easy_setopt(c, CURLOPT_REFERER,
                     referer.empty() ? nullptr : referer.c_str());
    if (body.empty()) {
      curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
      curl_easy_setopt(c, CURLOPT_POSTFIELDS, nullptr);
      curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    } else {
      curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
      curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)body.size());
    }
    auto ret = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    bool written = true;
    if (t.file)
      written = fclose(t.file) == 0;
    if (cancel)
      throw std::runtime_error("Cancelled");
    if (ret != CURLE_OK || code < 200 || code >= 300 || !written || !received)
      throw std::runtime_error(
          "Network/download failed; check connection and SD card, then retry");
    return t.body;
  }
};
std::string resolveItch(Client &c, DownloadPlan &p) {
  std::string html = c.request(p.source), page = p.source;
  std::string csrf =
      match(html, "name=[\"']csrf_token[\"'][^>]*content=[\"']([^\"']+)[\"']");
  if (csrf.empty())
    csrf = match(html,
                 "content=[\"']([^\"']+)[\"'][^>]*name=[\"']csrf_token[\"']");
  auto config =
      json(match(html, "init_View(?:Html)?Game\\([^,]+,\\s*(\\{.*?\\})\\);"));
  if (config.is<picojson::object>()) {
    const auto &game = config.get("game");
    int price = number(game, "actual_price");
    if (!price)
      price = number(game, "min_price");
    if (price > 0)
      p.demo = true;
    else if (flag(config, "generate_download_url")) {
      auto reply = json(c.request(p.source + "/download_url",
                                  "csrf_token=" + c.escape(csrf), p.source));
      page = field(reply, "url");
      if (!webUrl(page))
        return {};
      html = c.request(page, "", p.source);
      auto next = match(
          html, "name=[\"']csrf_token[\"'][^>]*content=[\"']([^\"']+)[\"']");
      if (!next.empty())
        csrf = next;
    }
  }
  std::string upload;
  if (!p.upload.empty() && std::regex_match(p.upload, std::regex("[0-9]+")) &&
      (html.find("data-upload_id=\"" + p.upload + "\"") != std::string::npos ||
       html.find("data-upload_id='" + p.upload + "'") != std::string::npos))
    upload = p.upload;
  if (upload.empty()) {
    std::regex tags("<a[^>]*>");
    for (std::sregex_iterator it(html.begin(), html.end(), tags), end;
         it != end; ++it) {
      auto tag = it->str();
      if (tag.find("download_btn") == std::string::npos)
        continue;
      auto id = match(tag, "data-upload_id=[\"']([0-9]+)[\"']");
      if (id.empty())
        continue;
      auto region = html.substr(it->position() + it->length(), 1500);
      auto next = region.find("<div class=\"upload\">");
      if (next != std::string::npos)
        region.resize(next);
      auto name =
          match(region, "class=[\"']name[\"'][^>]*title=[\"']([^\"']+)[\"']");
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (name.find(".pdx") != std::string::npos ||
          name.find("playdate") != std::string::npos) {
        upload = id;
        p.demo = p.demo || name.find("demo") != std::string::npos;
        break;
      }
    }
  }
  if (upload.empty())
    return {};
  auto endpoint = p.source + "/file/" + upload +
                  "?source=" + (p.demo ? "view_game" : "game_download") +
                  "&as_props=1";
  auto reply = json(c.request(endpoint, "csrf_token=" + c.escape(csrf), page));
  auto url = field(reply, "url");
  return webUrl(url) ? url : std::string();
}
} // namespace
std::string LauncherState::encode() const {
  picojson::object v;
  v["id"] = Json((double)id);
  v["state"] = Json(state);
  v["label"] = Json(label);
  v["message"] = Json(message);
  v["busy"] = Json(busy);
  v["received"] = Json((double)received);
  v["total"] = Json((double)total);
  return Json(v).serialize();
}
Launcher::Launcher(const std::string &root, const std::string &romfs)
    : root(root), romfs(romfs) {
  recoverInstalls(root + "/games");
  remove((root + "/.launcher-download.zip").c_str());
  auto data = json(readText(romfs + "/launcher-catalog.json", 4 * 1024 * 1024));
  if (data.is<picojson::object>() && data.get("games").is<picojson::object>())
    for (auto &item : data.get("games").get<picojson::object>()) {
      int id = atoi(item.first.c_str());
      auto &v = item.second;
      DownloadPlan p{field(v, "kind"), field(v, "source"), field(v, "upload"),
                     flag(v, "demo")};
      while (!p.source.empty() && p.source.back() == '/')
        p.source.pop_back();
      if (id > 0 && webUrl(p.source))
        catalog[id] = p;
    }
  data = json(readText(root + "/launcher-library.json"));
  if (data.is<picojson::object>())
    for (auto &item : data.get<picojson::object>()) {
      auto name = field(item.second, "directory");
      auto id = atoi(item.first.c_str());
      if (id > 0 && !name.empty() && name[0] != '.' &&
          name.find_first_of("/\\:") == std::string::npos &&
          inspectGame(root + "/games/" + name).supported)
        installed[id] = {root + "/games/" + name, flag(item.second, "demo")};
    }
}
Launcher::~Launcher() {
  cancel = true;
  if (thread)
    SDL_WaitThread(thread, nullptr);
}
bool Launcher::saveIndex() {
  picojson::object all;
  for (auto &item : installed) {
    picojson::object record;
    record["directory"] =
        Json(item.second.first.substr(item.second.first.find_last_of('/') + 1));
    record["demo"] = Json(item.second.second);
    all[std::to_string(item.first)] = Json(record);
  }
  return writeAtomic(root + "/launcher-library.json", Json(all).serialize());
}
void Launcher::select(int id) {
  current = {};
  current.id = id;
  launchPath.clear();
  autoLaunch = false;
  selectedPlan = {};
  if (auto it = installed.find(id);
      it != installed.end() && inspectGame(it->second.first).supported) {
    current.state = "installed";
    current.label = it->second.second ? "Run demo" : "Run game";
    current.message = "Installed; available offline";
    return;
  }
  if (auto it = catalog.find(id); it != catalog.end())
    selectedPlan = it->second;
  if (selectedPlan.source.empty()) {
    start(false);
    return;
  }
  bool ready = selectedPlan.kind == "direct" ||
               (selectedPlan.kind == "itch" && itchUrl(selectedPlan.source));
  current.state = ready ? "ready" : "unavailable";
  current.label =
      ready ? (selectedPlan.demo ? "Download demo" : "Download & Play")
            : "Publisher link";
  current.message =
      ready ? "Downloads publisher's public PDX package"
            : "Get the PDX from the publisher; copy it to your SD game library";
}
void Launcher::start(bool download) {
  if (thread)
    return;
  workPlan = selectedPlan;
  workSession = session;
  workId = current.id;
  workDemo = workPlan.demo;
  downloading = download;
  autoLaunch = download;
  cancel = false;
  done = false;
  stage = 0;
  received = 0;
  total = 0;
  installedPath.clear();
  result = {};
  result.id = workId;
  current.busy = true;
  current.state = "checking";
  current.label = download ? "Preparing..." : "Checking...";
  thread =
      SDL_CreateThreadWithStackSize(run, "PDGames download", 1024 * 1024, this);
  if (!thread) {
    current.busy = false;
    current.state = "error";
    current.label = "Retry download";
    current.message = "Cannot start download worker";
  }
}
int Launcher::run(void *self) {
  ((Launcher *)self)->work();
  ((Launcher *)self)->done = true;
  return 0;
}
void Launcher::work() {
  std::string archive = root + "/.launcher-download.zip";
  try {
    Client c(romfs + "/cacert.pem", cancel, received, total);
    if (!downloading) {
      auto detail = json(c.request("https://pdgamez.com/game/" +
                                   std::to_string(workId) + ".json"));
      if (!detail.is<picojson::object>() ||
          !detail.get("urls").is<picojson::object>())
        throw std::runtime_error("Game source unavailable; retry");
      auto &urls = detail.get("urls");
      workPlan.source = field(urls, "itch");
      workPlan.kind = "itch";
      if (!itchUrl(workPlan.source)) {
        workPlan.source = field(urls, "catalog");
        workPlan.kind = "publisher";
      }
      while (!workPlan.source.empty() && workPlan.source.back() == '/')
        workPlan.source.pop_back();
      result.state = workPlan.kind == "itch" ? "ready" : "unavailable";
      result.label =
          result.state == "ready" ? "Download & Play" : "Publisher link";
      result.message = result.state == "ready"
                           ? "Download the publisher's public PDX package"
                           : "Get the PDX from the publisher; copy it to your "
                             "SD game library";
    } else {
      auto url = workPlan.kind == "direct" ? workPlan.source
                                           : resolveItch(c, workPlan);
      if (url.empty()) {
        result.state = "unavailable";
        result.label = "Publisher link";
        result.message =
            "No public PDX ZIP found; obtain the game from the publisher";
      } else {
        stage = 1;
        c.request(url, "", workPlan.source, archive);
        stage = 2;
        std::string error;
        installedPath =
            installArchive(archive, root + "/games", workId, cancel, error);
        if (installedPath.empty())
          throw std::runtime_error(error);
        workDemo = workPlan.demo;
        result.state = "installed";
        result.label = workDemo ? "Run demo" : "Run game";
        result.message = "Installed; available offline";
      }
    }
  } catch (const std::exception &e) {
    result.state = "error";
    result.label = "Retry download";
    result.message = e.what();
  }
  remove(archive.c_str());
}
void Launcher::leave() {
  current.id = 0;
  pendingId = 0;
  autoLaunch = false;
  launchPath.clear();
}
void Launcher::command(uint32_t token, int id, const std::string &action) {
  if (token != session) {
    session = token;
    autoLaunch = false;
    pendingId = 0;
    launchPath.clear();
    if (thread)
      cancel = true;
  }
  if (action == "inspect") {
    if (thread) {
      pendingId = id;
      current = {};
      current.id = id;
      autoLaunch = false;
      cancel = true;
    } else
      select(id);
  } else if (action == "leave")
    leave();
  else if (id == current.id && action == "cancel" && thread) {
    cancel = true;
    autoLaunch = false;
  } else if (id == current.id && action == "play" && !thread) {
    auto it = installed.find(id);
    if (it != installed.end() && inspectGame(it->second.first).supported)
      launchPath = it->second.first;
    else if (current.state == "ready" || current.state == "error") {
      if (selectedPlan.source.empty())
        select(id);
      else
        start(true);
    }
  }
}
std::string Launcher::tick(bool allow) {
  if (thread && done) {
    SDL_WaitThread(thread, nullptr);
    thread = nullptr;
    if (!installedPath.empty()) {
      installed[workId] = {installedPath, workDemo};
      if (!saveIndex())
        result.message =
            "Installed in game library; catalog link could not be saved";
      if (autoLaunch && allow && workSession == session && current.id == workId)
        launchPath = installedPath;
    }
    if (workSession == session && current.id == workId) {
      current = result;
      selectedPlan = workPlan;
      if (cancel && installedPath.empty()) {
        current.state = "ready";
        current.label = "Download & Play";
        current.message = "Cancelled; press A to retry";
      } else if (!workPlan.source.empty())
        catalog[workId] = workPlan;
    }
    if (pendingId) {
      int id = pendingId;
      pendingId = 0;
      select(id);
    }
  }
  if (!allow) {
    autoLaunch = false;
    launchPath.clear();
  }
  auto path = launchPath;
  launchPath.clear();
  return path;
}
LauncherState Launcher::status() const {
  auto s = current;
  s.busy = thread != nullptr;
  if (thread && current.id == workId) {
    s.busy = true;
    s.received = received;
    s.total = total;
    s.message = stage == 2   ? "Validating and installing..."
                : stage == 1 ? "Downloading..."
                             : "Querying publisher...";
    if (cancel)
      s.message = "Cancelling...";
  }
  return s;
}
} // namespace SwitchApp
