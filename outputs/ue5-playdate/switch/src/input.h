#pragma once
#include <cstdint>
namespace SwitchApp {
struct CrankInput {
  bool tracking = false, docked = true;
  float previous = 0;
  float update(float x, float y, bool clockwise, bool counterclockwise,
               bool toggle, float seconds, float speed = 360);
  void release() { tracking = false; }
};
struct Rect {
  int x, y, w, h;
};
Rect fitDisplay(int width, int height, bool integerScale);
} // namespace SwitchApp
