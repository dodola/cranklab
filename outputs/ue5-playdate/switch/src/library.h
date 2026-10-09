#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace SwitchApp {
struct Game {
  std::string path, name, author, bundle, saveKey, error;
  bool supported = false;
};
bool mkdirs(const std::string &path);
std::string readText(const std::string &path, size_t limit = 1024 * 1024);
bool writeAtomic(const std::string &path, const std::string &content);
Game inspectGame(const std::string &path);
std::vector<Game> scanLibrary(const std::string &root);
} // namespace SwitchApp
