#define SDL_MAIN_HANDLED
#include "crank_host.h"
#include "input.h"
#include "json.h"
#include "launcher.h"
#include "library.h"
#include "theme.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <curl/curl.h>
#include <memory>
#include <sstream>
#ifdef __SWITCH__
#include <switch.h>
#endif
extern "C" {
#include "cranklab_menu.h"
void crank_switch_volume(int percent);
}
using namespace SwitchApp;
namespace {
enum class Panel { Game, Library, Settings, Error, Menu };
struct Controls {
  uint32_t game = 0;
  bool accept = false, back = false, menu = false, library = false,
       settings = false, home = false, up = false, down = false, left = false,
       right = false, dock = false, cw = false, ccw = false;
  float x = 0, y = 0;
};
struct App {
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *display = nullptr;
  SDL_GameController *controller = nullptr;
  TTF_Font *font = nullptr, *title = nullptr;
  std::string root, launcher, error, status, currentPath, currentName;
  Panel panel = Panel::Game, previous = Panel::Library;
  std::vector<Game> library;
  std::unique_ptr<Launcher> downloads;
  uint32_t launcherSession = 0;
  std::string lastLauncherReply;
  int selected = 0, setting = 0, menuSelected = 0, customCount = 0;
  bool running = true, integerScale = false, haptics = true, background = false,
       launcherRunning = false;
  int volume = 100;
  float crankSpeed = 360;
  CrankInput crank;
  uint32_t frame = 0;
  uint8_t pixels[400 * 240 * 4]{};
  CrankMenuModel menuModel{};
  uint32_t menuPixels[400 * 240];
  uint8_t menuImage[400 * 240 * 4];
  SDL_Rect gameRect{};
  uint32_t blocked = 0;
  uint32_t sentButtons = 0;
  uint64_t lastHaptic = 0;
#ifdef __SWITCH__
  PadState pad{};
#endif
  void color(SDL_Color c) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
  }
  void box(SDL_Rect r, SDL_Color c) {
    color(c);
    SDL_RenderFillRect(renderer, &r);
  }
  void text(const std::string &s, int x, int y, SDL_Color c,
            TTF_Font *f = nullptr) {
    if (!font || s.empty())
      return;
    auto *surface = TTF_RenderUTF8_Blended(f ? f : font, s.c_str(), c);
    if (!surface)
      return;
    auto *t = SDL_CreateTextureFromSurface(renderer, surface);
    if (t) {
      SDL_Rect r{x, y, surface->w, surface->h};
      SDL_RenderCopy(renderer, t, nullptr, &r);
      SDL_DestroyTexture(t);
    }
    SDL_FreeSurface(surface);
  }
  void fail(const std::string &s) {
    error = s;
    previous =
        panel == Panel::Game && (currentPath.empty() || crank_status() == 3)
            ? Panel::Library
            : panel;
    if (previous == Panel::Library)
      library = scanLibrary(root + "/games");
    panel = Panel::Error;
    release();
    pause();
  }
  void release() {
    crank_set_buttons(0);
    sentButtons = 0;
    crank.release();
    blocked = 63;
  }
  void pause() {
    crank_set_suspended((panel == Panel::Game ? 0 : 1) | (background ? 2 : 0));
  }
  void open(Panel p) {
    panel = p;
    selected = 0;
    release();
    pause();
    if (p == Panel::Library)
      library = scanLibrary(root + "/games");
  }
  void close() {
    panel = currentPath.empty() ? Panel::Library : Panel::Game;
    release();
    crank_set_menu_open(0);
    pause();
  }
  bool launch(const std::string &p, bool asLauncher = false) {
    auto g = inspectGame(p);
    if (!g.supported) {
      fail(g.error);
      return false;
    }
    const int state = crank_status();
    if (p == currentPath && (state == 1 || state == 2 || state == 4)) {
      close();
      return true;
    }
    auto saves = root + "/saves/" + g.saveKey;
    if (!mkdirs(saves)) {
      fail("无法创建存档目录，请检查 SD 卡空间。");
      return false;
    }
    release();
    if (downloads)
      downloads->leave();
    crank_stop();
    frame = 0;
    currentPath.clear();
    launcherRunning = false;
    lastLauncherReply.clear();
    launcherSession = 0;
    if (!(asLauncher ? crank_start_launcher(p.c_str(), saves.c_str())
                     : crank_start(p.c_str(), saves.c_str()))) {
      fail("游戏无法启动。");
      return false;
    }
    currentPath = p;
    currentName = g.name;
    launcherRunning = asLauncher;
    panel = Panel::Game;
    pause();
    return true;
  }
  void systemMenu() {
    if (currentPath.empty()) {
      open(Panel::Library);
      return;
    }
    open(Panel::Menu);
    crank_set_menu_open(1);
    menuSelected = 0;
    char json[16384];
    crank_copy_menu(json, sizeof(json));
    cranklab_menu_parse(json, &menuModel);
    customCount = menuModel.count;
    for (auto name : {"PDGames", "Local library", "Settings"}) {
      if (menuModel.count >= MENU_MAX_ITEMS)
        break;
      auto &i = menuModel.items[menuModel.count++];
      memset(&i, 0, sizeof(i));
      i.id = -menuModel.count;
      snprintf(i.title, sizeof(i.title), "%s", name);
    }
  }
  void rumble() {
    if (!haptics || !controller || SDL_GetTicks64() - lastHaptic < 50)
      return;
    SDL_GameControllerRumble(controller, 9000, 18000, 25);
    lastHaptic = SDL_GetTicks64();
  }
  Controls controls() {
    Controls c;
    static Uint32 old = 0;
    Uint32 held = 0, down = 0;
#ifdef __SWITCH__
    padUpdate(&pad);
    auto h = padGetButtons(&pad), d = padGetButtonsDown(&pad);
    if (h & HidNpadButton_Left)
      c.game |= 1;
    if (h & HidNpadButton_Right)
      c.game |= 2;
    if (h & HidNpadButton_Up)
      c.game |= 4;
    if (h & HidNpadButton_Down)
      c.game |= 8;
    if (h & HidNpadButton_B)
      c.game |= 16;
    if (h & HidNpadButton_A)
      c.game |= 32;
    c.accept = d & HidNpadButton_A;
    c.back = d & HidNpadButton_B;
    c.menu = d & HidNpadButton_Plus;
    c.home = d & HidNpadButton_Minus;
    c.library = d & HidNpadButton_Y;
    c.settings = d & HidNpadButton_X;
    c.up = d & HidNpadButton_Up;
    c.down = d & HidNpadButton_Down;
    c.left = d & HidNpadButton_Left;
    c.right = d & HidNpadButton_Right;
    c.cw = h & HidNpadButton_ZR;
    c.ccw = h & HidNpadButton_ZL;
    c.dock = d & HidNpadButton_StickR;
    auto stick = padGetStickPos(&pad, 1);
    c.x = stick.x / 32767.f;
    c.y = -stick.y / 32767.f;
#else
    const Uint8 *k = SDL_GetKeyboardState(nullptr);
    if (k[SDL_SCANCODE_LEFT])
      c.game |= 1;
    if (k[SDL_SCANCODE_RIGHT])
      c.game |= 2;
    if (k[SDL_SCANCODE_UP])
      c.game |= 4;
    if (k[SDL_SCANCODE_DOWN])
      c.game |= 8;
    if (k[SDL_SCANCODE_S])
      c.game |= 16;
    if (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_RETURN])
      c.game |= 32;
    for (int i = 0; i < 12; i++) {
      const SDL_Scancode keys[] = {
          SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_M,
          SDL_SCANCODE_Y,      SDL_SCANCODE_X,      SDL_SCANCODE_H,
          SDL_SCANCODE_UP,     SDL_SCANCODE_DOWN,   SDL_SCANCODE_LEFT,
          SDL_SCANCODE_RIGHT,  SDL_SCANCODE_D,      SDL_SCANCODE_A};
      if (k[keys[i]])
        held |= 1u << i;
    }
    if (controller) {
      const SDL_GameControllerButton buttons[] = {
          SDL_CONTROLLER_BUTTON_A,         SDL_CONTROLLER_BUTTON_B,
          SDL_CONTROLLER_BUTTON_START,     SDL_CONTROLLER_BUTTON_Y,
          SDL_CONTROLLER_BUTTON_X,         SDL_CONTROLLER_BUTTON_BACK,
          SDL_CONTROLLER_BUTTON_DPAD_UP,   SDL_CONTROLLER_BUTTON_DPAD_DOWN,
          SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
          SDL_CONTROLLER_BUTTON_RIGHTSTICK};
      for (int i = 0; i < 11; i++)
        if (SDL_GameControllerGetButton(controller, buttons[i]))
          held |= 1u << i;
      if (held & 1)
        c.game |= 32;
      if (held & 2)
        c.game |= 16;
      if (held & (1 << 6))
        c.game |= 4;
      if (held & (1 << 7))
        c.game |= 8;
      if (held & (1 << 8))
        c.game |= 1;
      if (held & (1 << 9))
        c.game |= 2;
      c.x = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX) /
            32767.f;
      c.y = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY) /
            32767.f;
      c.cw = SDL_GameControllerGetAxis(
                 controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000;
      c.ccw = SDL_GameControllerGetAxis(
                  controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000;
    }
    down = held & ~old;
    old = held;
    c.accept = down & ((1 << 0) | (1 << 11));
    c.back = down & (1 << 1);
    c.menu = down & (1 << 2);
    c.library = down & (1 << 3);
    c.settings = down & (1 << 4);
    c.home = down & (1 << 5);
    c.up = down & (1 << 6);
    c.down = down & (1 << 7);
    c.left = down & (1 << 8);
    c.right = down & (1 << 9);
    c.dock = down & (1 << 10);
    c.cw = c.cw || k[SDL_SCANCODE_E];
    c.ccw = c.ccw || k[SDL_SCANCODE_Q];
#endif
    (void)old;
    (void)held;
    (void)down;
    return c;
  }
  bool settingsSave() {
    if (!writeAtomic(root + "/settings.txt",
                     "integer=" + std::to_string(integerScale) +
                         "\nhaptics=" + std::to_string(haptics) +
                         "\nvolume=" + std::to_string(volume) +
                         "\nspeed=" + std::to_string((int)crankSpeed) + "\n")) {
      fail("设置未能保存，请检查 SD 卡空间后重试。");
      return false;
    }
    return true;
  }
  void handle(Controls c, float dt) {
    blocked &= c.game;
    if (background) {
      release();
      return;
    }
    if (panel == Panel::Error) {
      if (c.back || c.accept) {
        panel = previous;
        pause();
      }
      return;
    }
    if (c.home) {
      launch(launcher, true);
      return;
    }
    if (c.library) {
      open(Panel::Library);
      return;
    }
    if (c.settings) {
      open(Panel::Settings);
      return;
    }
    if (c.menu) {
      if (panel == Panel::Game)
        systemMenu();
      else
        close();
      return;
    }
    if (panel == Panel::Library) {
      if (c.back) {
        close();
        return;
      }
      if (!library.empty()) {
        selected = std::clamp(selected + (c.down ? 1 : 0) - (c.up ? 1 : 0), 0,
                              (int)library.size() - 1);
        if (c.accept)
          launch(library[selected].path);
      }
    } else if (panel == Panel::Settings) {
      if (c.back) {
        if (settingsSave())
          close();
        return;
      }
      setting = std::clamp(setting + (c.down ? 1 : 0) - (c.up ? 1 : 0), 0, 3);
      int d = (c.right ? 1 : 0) - (c.left ? 1 : 0);
      if (c.accept || d) {
        if (setting == 0)
          integerScale = !integerScale;
        if (setting == 1)
          haptics = !haptics;
        if (setting == 2)
          volume = std::clamp(volume + (d ? d : 1) * 10, 0, 100);
        if (setting == 3)
          crankSpeed = std::clamp(crankSpeed + (d ? d : 1) * 90, 90.f, 720.f);
        crank_switch_volume(volume);
        settingsSave();
        rumble();
      }
    } else if (panel == Panel::Menu) {
      int rows = cranklab_menu_rows(&menuModel);
      menuSelected =
          (menuSelected + (c.down ? 1 : 0) - (c.up ? 1 : 0) + rows) % rows;
      if (c.back) {
        close();
        return;
      }
      if (menuSelected < customCount) {
        auto &i = menuModel.items[menuSelected];
        if (c.accept || c.left || c.right) {
          if (i.kind == 0) {
            crank_menu_select(i.id, 0);
            close();
          } else {
            i.value =
                i.kind == 1
                    ? !i.value
                    : 1 + (i.value - 1 + (c.left ? -1 : 1) + i.option_count) %
                              std::max(1, i.option_count);
            crank_menu_select(i.id, i.value);
          }
        }
      } else if (c.accept) {
        int action = menuSelected - customCount;
        if (menuSelected == rows - 1)
          close();
        else if (action == 0)
          launch(launcher, true);
        else if (action == 1)
          open(Panel::Library);
        else
          open(Panel::Settings);
      }
    } else {
      const uint32_t mask = c.game & ~blocked;
      if (mask & ~sentButtons)
        rumble();
      crank_set_buttons(mask);
      sentButtons = mask;
      auto delta = crank.update(c.x, c.y, c.cw, c.ccw, c.dock, dt, crankSpeed);
      crank_add_crank(delta, crank.docked);
      if (delta || c.dock)
        rumble();
    }
  }
  void service() {
    char request[256];
    while (crank_launcher_request(request, sizeof(request))) {
      auto v = json(request);
      int id = number(v, "id");
      auto action = field(v, "action");
      launcherSession = number(v, "session");
      downloads->command(launcherSession, id, action);
      if (action == "library")
        open(Panel::Library);
      if (action == "publisher") {
        auto url = downloads->publisher();
        if (url.empty())
          fail("发布者地址不可用，请从本地游戏库运行已取得的游戏。");
        else {
          release();
          crank_set_suspended(1);
#ifdef __SWITCH__
          WebCommonConfig config;
          WebCommonReply reply;
          Result result = webPageCreate(&config, url.c_str());
          if (R_SUCCEEDED(result))
            result = webConfigShow(&config, &reply);
          if (R_FAILED(result))
            fail("无法打开系统浏览器。可在其他设备取得 PDX 后复制到 SD 卡。\n" +
                 url);
#else
          if (SDL_OpenURL(url.c_str()) != 0)
            fail("无法打开浏览器。\n" + url);
#endif
          release();
          pause();
        }
      }
    }
    auto path =
        downloads->tick(launcherRunning && panel == Panel::Game && !background);
    if (!path.empty())
      launch(path);
    if (launcherRunning) {
      auto reply = downloads->status().encode();
      if (reply != lastLauncherReply) {
        if (crank_launcher_reply(launcherSession, reply.c_str()))
          lastLauncherReply = reply;
      }
    }
  }
  void draw() {
    int w, h;
    SDL_GetRendererOutputSize(renderer, &w, &h);
    color(Theme::Accent);
    SDL_RenderClear(renderer);
    auto r = fitDisplay(w, h, integerScale);
    gameRect = {r.x, r.y, r.w, r.h};
    auto serial = crank_copy_frame(pixels, sizeof(pixels));
    if (serial) {
      frame = serial;
      SDL_UpdateTexture(display, nullptr, pixels, 400 * 4);
      SDL_RenderCopy(renderer, display, nullptr, &gameRect);
    }
    if (panel == Panel::Menu) {
      cranklab_menu_draw(menuPixels, &menuModel, menuSelected,
                         crank_copy_menu_image(menuImage, sizeof(menuImage))
                             ? menuImage
                             : nullptr,
                         menuModel.image_offset, 0x302e29, 0xd4d2c9);
      SDL_UpdateTexture(display, nullptr, menuPixels, 400 * 4);
      SDL_RenderCopy(renderer, display, nullptr, &gameRect);
    } else if (panel != Panel::Game) {
      box({0, 0, w, h}, Theme::HudSurface);
      int edge = std::max(24, w / 20), top = 28;
      const char *heading = panel == Panel::Library    ? "本地游戏库"
                            : panel == Panel::Settings ? "设置"
                                                       : "无法完成操作";
      text(heading, edge, top, Theme::HudText, title);
      text("PLAYDATE", w - edge - 170, top + 10, Theme::Accent);
      box({edge, 90, w - edge * 2, 2}, Theme::HudFrame);
      if (panel == Panel::Library) {
        int visible = std::max(1, (h - 210) / 78),
            start = std::max(0, selected - visible + 1);
        if (library.empty()) {
          text("还没有本地游戏。", edge, 130, Theme::HudText);
          text("将 Lua 游戏的 .pdx 文件夹放入 /switch/playdate/games", edge,
               175, Theme::HudMuted);
        }
        for (int i = start; i < (int)library.size() && i < start + visible;
             i++) {
          int y = 112 + (i - start) * 78;
          auto &g = library[i];
          bool focus = i == selected;
          if (focus)
            box({edge - 10, y - 4, w - edge * 2 + 20, 72}, Theme::HudRaised);
          if (focus)
            box({edge - 10, y - 4, 4, 72}, Theme::Accent);
          SDL_Rect clip{edge, y, w - edge * 2 - 80, 70};
          SDL_RenderSetClipRect(renderer, &clip);
          text(g.name, edge + 12, y, Theme::HudText);
          text(g.supported ? g.author : g.error, edge + 12, y + 34,
               g.supported ? Theme::HudMuted : Theme::HudError);
          SDL_RenderSetClipRect(renderer, nullptr);
          if (focus)
            text("A", w - edge - 36, y + 12, Theme::Accent);
        }
        if (!library.empty())
          text(std::to_string(selected + 1) + " / " +
                   std::to_string(library.size()),
               w - edge - 100, h - 85, Theme::HudMuted);
      } else if (panel == Panel::Settings) {
        std::string rows[] = {
            std::string("整数倍显示：") + (integerScale ? "开" : "关"),
            std::string("按键与摇柄震动：") + (haptics ? "开" : "关"),
            "音量：" + std::to_string(volume) + "%",
            "摇柄速度：" + std::to_string((int)crankSpeed) + "°/秒"};
        for (int i = 0; i < 4; i++) {
          int y = 124 + i * 82;
          if (setting == i) {
            box({edge - 10, y - 8, w - edge * 2 + 20, 66}, Theme::HudRaised);
            box({edge - 10, y - 8, 4, 66}, Theme::Accent);
          }
          text(rows[i], edge + 12, y, Theme::HudText);
        }
        text("右摇杆转圈或按住 ZL / ZR 转动摇柄；按下右摇杆收纳。", edge,
             h - 135, Theme::HudMuted);
      } else {
        std::string line;
        std::istringstream words(error);
        int y = 130;
        while (std::getline(words, line) && y < h - 120) {
          text(line.substr(0, 200), edge, y, Theme::HudError);
          y += 40;
        }
        text("按 B 返回，可选择其他 Lua 游戏。", edge, h - 130,
             Theme::HudMuted);
      }
      text("A 选择    B 返回    + 菜单    − PDGames    Y 游戏库    X 设置",
           edge, h - 55, Theme::HudMuted);
    }
    SDL_RenderPresent(renderer);
  }
};
} // namespace
int main(int argc, char **argv) {
  SDL_SetMainReady();
  static App a;
#ifdef __SWITCH__
  romfsInit();
  socketInitializeDefault();
  plInitialize(PlServiceType_User);
  padConfigureInput(1, HidNpadStyleSet_NpadFullCtrl);
  padInitializeAny(&a.pad);
  a.root = "sdmc:/switch/playdate";
  a.launcher = "romfs:/launcher.pdx";
#else
  a.root = "./playdate-data";
  a.launcher = "./romfs/launcher.pdx";
#endif
  int frames = 0;
  std::string screenshot;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--root" && i + 1 < argc)
      a.root = argv[++i];
    else if (arg == "--launcher" && i + 1 < argc)
      a.launcher = argv[++i];
    else if (arg == "--game" && i + 1 < argc)
      a.currentPath = argv[++i];
    else if (arg == "--frames" && i + 1 < argc)
      frames = atoi(argv[++i]);
    else if (arg == "--screenshot" && i + 1 < argc)
      screenshot = argv[++i];
    else if (arg == "--library")
      a.panel = Panel::Library;
    else if (arg == "--settings")
      a.panel = Panel::Settings;
  }
  if (!mkdirs(a.root + "/games") || !mkdirs(a.root + "/saves")) {
    fprintf(stderr, "Cannot create Playdate storage\n");
    return 1;
  }
  std::istringstream prefs(readText(a.root + "/settings.txt", 4096));
  std::string line;
  while (std::getline(prefs, line)) {
    auto pos = line.find('=');
    if (pos == std::string::npos)
      continue;
    auto key = line.substr(0, pos);
    int value = atoi(line.substr(pos + 1).c_str());
    if (key == "integer")
      a.integerScale = value != 0;
    if (key == "haptics")
      a.haptics = value != 0;
    if (key == "volume")
      a.volume = std::clamp(value, 0, 100);
    if (key == "speed")
      a.crankSpeed = std::clamp((float)value, 90.f, 720.f);
  }
  curl_global_init(CURL_GLOBAL_DEFAULT);
  a.downloads = std::make_unique<Launcher>(
      a.root, a.launcher.substr(0, a.launcher.find_last_of('/')));
  crank_switch_volume(a.volume);
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) !=
      0) {
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }
  TTF_Init();
  a.window =
      SDL_CreateWindow("Playdate", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE);
  a.renderer = SDL_CreateRenderer(
      a.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!a.renderer)
    a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_SOFTWARE);
  if (!a.window || !a.renderer) {
    fprintf(stderr, "Video: %s\n", SDL_GetError());
    return 1;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  a.display = SDL_CreateTexture(a.renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, 400, 240);
  SDL_SetRenderDrawBlendMode(a.renderer, SDL_BLENDMODE_BLEND);
#ifdef __SWITCH__
  PlFontData f{};
  if (R_SUCCEEDED(
          plGetSharedFontByType(&f, PlSharedFontType_ChineseSimplified))) {
    a.font = TTF_OpenFontRW(SDL_RWFromConstMem(f.address, f.size), 1, 26);
    a.title = TTF_OpenFontRW(SDL_RWFromConstMem(f.address, f.size), 1, 38);
  }
#else
  const char *font = getenv("PLAYDATE_UI_FONT");
  if (!font)
    font = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
  a.font = TTF_OpenFont(font, 26);
  a.title = TTF_OpenFont(font, 38);
#endif
  for (int i = 0; i < SDL_NumJoysticks(); i++)
    if (SDL_IsGameController(i)) {
      a.controller = SDL_GameControllerOpen(i);
      break;
    }
  auto initial = a.currentPath;
  a.currentPath.clear();
  auto initialPanel = a.panel;
  if (!initial.empty())
    a.launch(initial);
  else if (initialPanel == Panel::Game)
    a.launch(a.launcher, true);
  if (initialPanel != Panel::Game)
    a.open(initialPanel);
  uint64_t then = SDL_GetTicks64();
  int rendered = 0;
  while (a.running) {
#ifdef __SWITCH__
    if (!appletMainLoop())
      break;
    bool bg = appletGetFocusState() != AppletFocusState_InFocus;
    if (bg != a.background) {
      a.background = bg;
      a.release();
      a.pause();
    }
#endif
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT)
        a.running = false;
      if (e.type == SDL_WINDOWEVENT &&
          (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
           e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)) {
        a.background = e.window.event == SDL_WINDOWEVENT_FOCUS_LOST;
        a.release();
        a.pause();
      }
      if (e.type == SDL_CONTROLLERDEVICEADDED && !a.controller)
        a.controller = SDL_GameControllerOpen(e.cdevice.which);
      if (e.type == SDL_CONTROLLERDEVICEREMOVED && a.controller &&
          e.cdevice.which == SDL_JoystickInstanceID(
                                 SDL_GameControllerGetJoystick(a.controller))) {
        SDL_GameControllerClose(a.controller);
        a.controller = nullptr;
        a.release();
        if (a.panel == Panel::Game)
          a.systemMenu();
        else
          a.pause();
      }
    }
    auto now = SDL_GetTicks64();
    a.handle(a.controls(), (now - then) / 1000.f);
    a.service();
    then = now;
    if (crank_status() == 3 && a.panel == Panel::Game) {
      char error[2048];
      crank_last_error(error, sizeof(error));
      a.fail(error);
    }
    if (!a.background)
      a.draw();
    if (frames && ++rendered >= frames)
      break;
    SDL_Delay(a.background ? 50 : 1);
  }
  if (!screenshot.empty()) {
    int w, h;
    SDL_GetRendererOutputSize(a.renderer, &w, &h);
    auto *s =
        SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (s) {
      SDL_RenderReadPixels(a.renderer, nullptr, s->format->format, s->pixels,
                           s->pitch);
      SDL_SaveBMP(s, screenshot.c_str());
      SDL_FreeSurface(s);
    }
  }
  a.settingsSave();
  a.release();
  crank_stop();
  a.downloads.reset();
  cranklab_menu_free_font();
  if (a.controller)
    SDL_GameControllerClose(a.controller);
  if (a.font)
    TTF_CloseFont(a.font);
  if (a.title)
    TTF_CloseFont(a.title);
  SDL_DestroyTexture(a.display);
  SDL_DestroyRenderer(a.renderer);
  SDL_DestroyWindow(a.window);
  TTF_Quit();
  SDL_Quit();
  curl_global_cleanup();
#ifdef __SWITCH__
  plExit();
  socketExit();
  romfsExit();
#endif
  return 0;
}
