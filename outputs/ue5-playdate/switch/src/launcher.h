#pragma once
#include <SDL2/SDL.h>
#include <atomic>
#include <map>
#include <string>
namespace SwitchApp {
struct DownloadPlan {
  std::string kind, source, upload;
  bool demo = false;
};
struct LauncherState {
  int id = 0;
  std::string state = "checking", label = "Checking...", message;
  bool busy = false;
  uint64_t received = 0, total = 0;
  std::string encode() const;
};
class Launcher {
public:
  Launcher(const std::string &root, const std::string &romfs);
  ~Launcher();
  // All methods run on the UI thread. The worker only accesses immutable work
  // and atomics until SDL_WaitThread establishes completion.
  void command(uint32_t session, int id, const std::string &action);
  std::string tick(bool allowAutoLaunch);
  void leave();
  LauncherState status() const;
  std::string publisher() const { return selectedPlan.source; }

private:
  std::string root, romfs, launchPath;
  std::map<int, DownloadPlan> catalog;
  std::map<int, std::pair<std::string, bool>> installed;
  DownloadPlan selectedPlan, workPlan;
  LauncherState current, result;
  SDL_Thread *thread = nullptr;
  std::atomic<bool> cancel{false}, done{false};
  std::atomic<uint64_t> received{0}, total{0};
  std::atomic<int> stage{0};
  uint32_t session = 0, workSession = 0;
  int workId = 0, pendingId = 0;
  bool downloading = false, autoLaunch = false, workDemo = false;
  std::string installedPath;
  void select(int id);
  void start(bool download);
  static int run(void *self);
  void work();
  bool saveIndex();
};
} // namespace SwitchApp
