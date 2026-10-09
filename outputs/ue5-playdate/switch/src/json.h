#pragma once
#include "../vendor/picojson.h"
#include <string>
namespace SwitchApp {
using Json = picojson::value;
inline Json json(const std::string &text) {
  Json result;
  // Reject excessive recursion before entering the recursive parser.
  if (text.size() > 4 * 1024 * 1024)
    return Json();
  int depth = 0;
  bool quoted = false, escaped = false;
  for (char c : text) {
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == '{' || c == '[') {
      if (++depth > 32)
        return Json();
    } else if (c == '}' || c == ']') {
      if (--depth < 0)
        return Json();
    }
  }
  if (depth || quoted)
    return Json();
  if (!picojson::parse(result, text).empty())
    return Json();
  return result;
}
inline std::string field(const Json &v, const char *key) {
  if (!v.is<picojson::object>())
    return {};
  const auto &x = v.get(key);
  return x.is<std::string>() ? x.get<std::string>() : std::string();
}
inline int number(const Json &v, const char *key) {
  if (!v.is<picojson::object>())
    return 0;
  const auto &x = v.get(key);
  if (!x.is<double>() || x.get<double>() < 0 || x.get<double>() > 1000000000)
    return 0;
  return (int)x.get<double>();
}
inline bool flag(const Json &v, const char *key) {
  return v.is<picojson::object>() && v.get(key).is<bool>() &&
         v.get(key).get<bool>();
}
} // namespace SwitchApp
