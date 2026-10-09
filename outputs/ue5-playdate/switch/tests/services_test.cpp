#include "archive.h"
#include "json.h"
#include "launcher.h"
#include "library.h"
#include <SDL2/SDL.h>
#include <curl/curl.h>
#include <iostream>
using namespace SwitchApp;
static void check(bool ok, const std::string &message) {
  if (!ok) {
    std::cerr << "FAIL: " << message << "\n";
    exit(1);
  }
}
static std::string finish(Launcher &l, bool allow = true) {
  const auto deadline = SDL_GetTicks64() + 15000;
  std::string path;
  do {
    auto p = l.tick(allow);
    if (!p.empty())
      path = p;
    SDL_Delay(10);
  } while (l.status().busy && SDL_GetTicks64() < deadline);
  auto s = l.status();
  check(!s.busy,
        "worker completes / cancels without blocking UI: " + s.encode());
  return path;
}
int main(int argc, char **argv) {
  check(argc == 4, "fixtures, HTTPS URL, test output required");
  std::string fixtures = argv[1], url = argv[2], out = argv[3];
  SDL_Init(SDL_INIT_TIMER);
  curl_global_init(CURL_GLOBAL_DEFAULT);
  std::atomic<bool> cancel{false};
  auto spec = json(readText(fixtures + "/archives.json"));
  check(spec.is<picojson::object>(), "archive specifications");
  for (auto &item : spec.get<picojson::object>()) {
    std::string error;
    auto library = out + "/archive-" + item.first;
    auto installed = installArchive(fixtures + "/" + item.first + ".zip",
                                    library, 123, cancel, error);
    check(!installed.empty() == item.second.get<bool>(),
          "ZIP " + item.first + ": " + error);
    if (!installed.empty())
      check(inspectGame(installed).supported, "published game is Lua-only");
    check(scanLibrary(library).size() == (installed.empty() ? 0 : 1),
          "no partial game in library");
  }
  cancel = true;
  std::string error;
  check(
      installArchive(fixtures + "/good.zip", out + "/cancel", 1, cancel, error)
          .empty(),
      "cancelled install never publishes");
  cancel = false;
  picojson::object games;
  for (int id : {1, 2, 3, 4, 5}) {
    picojson::object p;
    p["kind"] = Json(std::string("direct"));
    p["source"] = Json(url + (id == 3   ? "/fail"
                              : id == 4 ? "/native.zip"
                              : id == 5 ? "/slow.zip"
                                        : "/good.zip"));
    p["demo"] = Json(id == 2);
    games[std::to_string(id)] = Json(p);
  }
  picojson::object catalog;
  catalog["games"] = Json(games);
  mkdirs(out + "/romfs");
  check(writeAtomic(out + "/romfs/launcher-catalog.json",
                    Json(catalog).serialize()),
        "catalog");
  check(
      writeAtomic(out + "/romfs/cacert.pem", readText(fixtures + "/cert.pem")),
      "test HTTPS CA");
  auto root = out + "/download";
  mkdirs(root + "/games");
  {
    Launcher l(root, out + "/romfs");
    l.command(11, 1, "inspect");
    check(l.status().state == "ready", "catalog source ready");
    l.command(11, 1, "play");
    l.command(11, 1, "play");
    auto path = finish(l);
    check(!path.empty() && inspectGame(path).supported,
          "download validates / installs / auto-launches once");
    check(l.tick(true).empty(), "launch consumed once");
    check(l.status().state == "installed", "installed status");
    l.command(11, 2, "inspect");
    l.command(11, 2, "play");
    l.command(11, 2, "leave");
    finish(l);
    l.command(11, 2, "inspect");
    check(l.status().label == "Run demo",
          "demo label retained; leaving detail suppresses automatic launch");
    l.command(11, 3, "inspect");
    l.command(11, 3, "play");
    check(finish(l).empty(), "HTTP failure cannot launch");
    check(l.status().state == "error" && !l.status().message.empty(),
          "failure visible and retryable");
    l.command(11, 4, "inspect");
    l.command(11, 4, "play");
    check(finish(l).empty(), "downloaded C bundle cannot launch");
    check(l.status().state == "error" &&
              scanLibrary(root + "/games").size() == 2,
          "C package does not install");
    l.command(11, 5, "inspect");
    l.command(11, 5, "play");
    auto deadline = SDL_GetTicks64() + 5000;
    while (l.status().received == 0 && SDL_GetTicks64() < deadline) {
      l.tick(true);
      SDL_Delay(10);
    }
    check(l.status().received > 0, "download byte progress");
    l.command(11, 5, "cancel");
    check(finish(l).empty(), "cancel does not launch");
    check(scanLibrary(root + "/games").size() == 2,
          "cancel keeps installed games");
    l.command(11, 5, "inspect");
    l.command(11, 5, "play");
    l.command(12, 1, "inspect");
    check(finish(l).empty() && l.status().id == 1 &&
              l.status().state == "installed",
          "old session completion cannot overwrite new selection");
    check(readText(root + "/.launcher-download.zip").empty(),
          "scratch download cleaned");
  }
  {
    Launcher l(root, out + "/romfs");
    l.command(20, 1, "inspect");
    check(l.status().state == "installed",
          "installed index survives restart / offline");
    l.command(20, 1, "play");
    check(!l.tick(true).empty(), "offline run uses local bundle");
    l.command(20, 2, "inspect");
    check(l.status().label == "Run demo", "demo index survives restart");
  }
  curl_global_cleanup();
  SDL_Quit();
  std::cout
      << "PASS: bounded streaming ZIP, corrupt/traversal/duplicate/native "
         "rejection, HTTPS download, install, progress, cancel, stale session, "
         "demo labels and offline library\n";
}
