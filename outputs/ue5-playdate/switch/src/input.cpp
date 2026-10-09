#include "input.h"
#include <algorithm>
#include <cmath>
namespace SwitchApp {
float CrankInput::update(float x, float y, bool cw, bool ccw, bool toggle,
                         float dt, float speed) {
  if (toggle)
    docked = !docked;
  float delta =
      ((cw ? 1 : 0) - (ccw ? 1 : 0)) * std::clamp(dt, 0.f, .1f) * speed;
  if (x * x + y * y >= .16f) {
    float angle = std::atan2(y, x) * 180.f / 3.14159265358979323846f;
    if (tracking) {
      float d = angle - previous;
      while (d > 180)
        d -= 360;
      while (d < -180)
        d += 360;
      delta += d;
    }
    previous = angle;
    tracking = true;
  } else
    tracking = false;
  if (std::abs(delta) > .001f)
    docked = false;
  return delta;
}
Rect fitDisplay(int w, int h, bool integerScale) {
  float scale = std::min(w / 400.f, h / 240.f);
  if (integerScale && scale >= 1)
    scale = std::floor(scale);
  int rw = std::max(1, (int)std::floor(400 * scale)),
      rh = std::max(1, (int)std::floor(240 * scale));
  return {(w - rw) / 2, (h - rh) / 2, rw, rh};
}
} // namespace SwitchApp
