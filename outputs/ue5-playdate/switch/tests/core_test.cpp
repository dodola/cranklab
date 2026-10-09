#include "crank_host.h"
#include "input.h"
#include "json.h"
#include "library.h"
#include "platform.h"
#include <SDL2/SDL.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <sys/stat.h>
using namespace SwitchApp;
extern "C" void crank_switch_volume(int percent);
static void check(bool ok, const char *what) {
  if (!ok) {
    std::cerr << "FAIL: " << what << "\n";
    exit(1);
  }
}
static void awaitFrame() {
  for (int n = 0; n < 300; n++) {
    uint8_t frame[400 * 240 * 4];
    if (crank_copy_frame(frame, sizeof(frame)))
      return;
    if (crank_status() == 3)
      break;
    SDL_Delay(10);
  }
  char e[2048];
  crank_last_error(e, sizeof(e));
  std::cerr << e << "\n";
  check(false, "Lua frame");
}
int main(int argc, char **argv) {
  check(argc == 2, "test root");
  std::string root = argv[1];
  remove((root + "/game/pdex.bin").c_str());
  check(mkdirs(root + "/game"), "mkdir");
  check(writeAtomic(
            root + "/game/pdxinfo",
            "name=Fixture\nauthor=CrankLab\nbundleID=local.switch.fixture\n"),
        "metadata");
  check(
      writeAtomic(
          root + "/game/main.lua",
          "local prior=playdate.datastore.read('visits') or {count=0};"
          "prior.count=prior.count+1;playdate.datastore.write(prior,'visits');"
          "local "
          "tone=assert(playdate.sound.sampleplayer.new('tone'));tone:play(0);"
          "playdate.getSystemMenu():addCheckmarkMenuItem('Check',false,"
          "function(v) playdate.datastore.write({value=v},'checked') end);"
          "function playdate.update() "
          "playdate.graphics.clear(playdate.graphics.kColorWhite);"
          "playdate.graphics.fillRect(10,10,30,30) end\n"),
      "Lua");
  std::string wave = "RIFF";
  auto le = [&](uint32_t v, int n) {
    for (int i = 0; i < n; i++)
      wave.push_back((char)(v >> (i * 8)));
  };
  le(36 + 44100 * 2, 4);
  wave += "WAVEfmt ";
  le(16, 4);
  le(1, 2);
  le(1, 2);
  le(44100, 4);
  le(88200, 4);
  le(2, 2);
  le(16, 2);
  wave += "data";
  le(88200, 4);
  for (int i = 0; i < 44100; i++)
    le((uint16_t)(int16_t)(10000 *
                           std::sin(2 * 3.141592653589793 * 440 * i / 44100)),
       2);
  check(writeAtomic(root + "/game/tone.wav", wave), "wave fixture");
  const std::string datastore = root + "/save/local.switch.fixture";
  remove((datastore + "/visits.json").c_str());
  auto game = inspectGame(root + "/game");
  check(game.supported, "Lua accepted");
  check(mkdirs(root + "/save"), "save root");
  check(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) == 0, "host SDL");
  auto *window = SDL_CreateWindow("host", 0, 0, 400, 240, SDL_WINDOW_HIDDEN);
  check(window, "host window");
  for (int cycle = 0; cycle < 3; cycle++) {
    check(crank_start(game.path.c_str(), (root + "/save").c_str()), "start");
    awaitFrame();
    SDL_Delay(80);
    int16_t pcm[8192];
    uint32_t count = crank_copy_audio(pcm, 8192);
    int peak = 0;
    for (uint32_t i = 0; i < count; i++)
      peak = std::max(peak, std::abs((int)pcm[i]));
    check(count && peak > 1000, "audible PCM from real mixer");
    check(number(json(readText(datastore + "/visits.json")), "count") ==
              cycle + 1,
          "datastore persists across restarts");
    crank_switch_volume(0);
    while (crank_copy_audio(pcm, 8192)) {
    };
    SDL_Delay(100);
    count = crank_copy_audio(pcm, 8192);
    peak = 0;
    for (uint32_t i = 0; i < count; i++)
      peak = std::max(peak, std::abs((int)pcm[i]));
    check(count && peak == 0, "host master mute affects PCM");
    crank_switch_volume(100);
    crank_set_suspended(1);
    for (int n = 0; n < 100 && crank_status() != 4; n++)
      SDL_Delay(10);
    check(crank_status() == 4, "suspend acknowledged");
    uint8_t frame[400 * 240 * 4];
    auto before = crank_copy_frame(frame, sizeof(frame));
    SDL_Delay(80);
    check(before == crank_copy_frame(frame, sizeof(frame)),
          "frozen while suspended");
    check(crank_copy_audio(pcm, 8192) == 0, "audio frozen while suspended");
    crank_set_menu_open(1);
    SDL_Delay(50);
    char menu[16384];
    check(crank_copy_menu(menu, sizeof(menu)) > 0, "SDK menu snapshot");
    auto model = json(menu);
    check(model.is<picojson::object>() &&
              model.get("items").is<picojson::array>(),
          "SDK menu JSON");
    auto item = model.get("items").get<picojson::array>().front();
    check(crank_menu_select(number(item, "id"), 1) == 1,
          "SDK check callback accepted");
    crank_set_menu_open(0);
    SDL_Delay(80);
    check(flag(json(readText(datastore + "/checked.json")), "value"),
          "SDK callback persists check value after closing menu");
    crank_set_suspended(0);
    SDL_Delay(100);
    check(crank_copy_frame(frame, sizeof(frame)) > before, "resume");
    crank_stop();
    check((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) != 0,
          "host video survives game stop");
    check(SDL_GetWindowID(window) != 0, "host window survives game stop");
  }
  check(writeAtomic(root + "/game/pdex.bin", "C"), "native marker");
  check(!inspectGame(root + "/game").supported, "mixed bundle rejected");
  check(crank_start(game.path.c_str(), (root + "/save").c_str()),
        "native start error session");
  for (int n = 0; n < 100 && crank_status() != 3; n++)
    SDL_Delay(10);
  check(crank_status() == 3, "C blocked by runtime too");
  crank_stop();
  remove((root + "/game/pdex.bin").c_str());
  auto first = game.saveKey;
  check(writeAtomic(root + "/game/pdxinfo",
                    "name=Renamed\nauthor=CrankLab\nbundleID=local.switch."
                    "fixture\nversion=2\n"),
        "upgrade metadata");
  check(inspectGame(game.path).saveKey == first,
        "stable save key across upgrades");
  CrankInput c;
  check(c.update(1, 0, false, false, false, .016f) == 0,
        "stick first sample no jump");
  check(std::abs(c.update(0, 1, false, false, false, .016f) - 90) < .01f,
        "stick clockwise");
  c.release();
  c.update(-1, .01, false, false, false, .016);
  check(std::abs(c.update(-1, -.01, false, false, false, .016)) < 2,
        "angle wrapping");
  c.release();
  check(std::abs(c.update(0, 0, true, false, false, 1.f / 60) - 6) < .01f,
        "held crank time based");
  check(c.update(0, 0, true, true, false, .016) == 0,
        "opposing triggers cancel");
  auto r = fitDisplay(1280, 720, true);
  check(r.w == 1200 && r.h == 720 && r.x == 40, "handheld integer fit");
  r = fitDisplay(1920, 1080, true);
  check(r.w == 1600 && r.h == 960, "dock integer fit");
  r = fitDisplay(300, 180, true);
  check(r.w <= 300 && r.h <= 180, "small window fit");
  check(scanLibrary(root).size() == 1, "library scan");
  for (int year : {1969, 1970, 2000, 2024, 2100})
    for (int month : {-1, 0, 1, 11, 12}) {
      struct tm t {};
      t.tm_year = year - 1900;
      t.tm_mon = month;
      t.tm_mday = 29;
      t.tm_hour = 13;
      auto expected = timegm(&t);
      check(pd_switch_timegm(&t) == expected,
            "Switch UTC conversion matches Gregorian calendar");
    }
  check(!json(std::string(40, '[') + "0" + std::string(40, ']'))
             .is<picojson::array>(),
        "JSON depth bounded");
  SDL_DestroyWindow(window);
  SDL_Quit();
  std::cout
      << "PASS: Lua frames, reject C/mixed games, game switching keeps SDL "
         "host, suspend/resume, datastore, audio/mute, SDK callback, crank, "
         "UTC and display geometry\n";
}
