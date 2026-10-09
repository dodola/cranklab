#pragma once
#include <atomic>
#include <string>
namespace SwitchApp {
void recoverInstalls(const std::string& library);
// Publishes only one validated Lua bundle; never modifies an existing
// game/save.
std::string installArchive(const std::string &archive,
                           const std::string &library, int catalogId,
                           std::atomic<bool> &cancelled, std::string &error);
} // namespace SwitchApp
