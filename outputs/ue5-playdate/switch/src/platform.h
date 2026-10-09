#pragma once
#include <time.h>
/* Newlib lacks timegm. Gregorian civil-date conversion avoids changing the
   process-wide timezone while the game and HTTP threads are active. */
static inline time_t pd_switch_timegm(const struct tm *t) {
  long long y = (long long)t->tm_year + 1900, m = t->tm_mon;
  y += m >= 0 ? m / 12 : (m - 11) / 12;
  m = ((m % 12) + 12) % 12 + 1;
  y -= m <= 2;
  long long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + t->tm_mday - 1;
  long long days =
      era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
  return (time_t)(days * 86400 + t->tm_hour * 3600 + t->tm_min * 60 +
                  t->tm_sec);
}
#ifdef __SWITCH__
#define timegm pd_switch_timegm
#endif
