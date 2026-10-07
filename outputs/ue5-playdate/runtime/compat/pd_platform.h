#pragma once
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
static struct tm *pd_localtime_r(const time_t *t, struct tm *result) {
    return localtime_s(result, t) == 0 ? result : NULL;
}
static struct tm *pd_gmtime_r(const time_t *t, struct tm *result) {
    return gmtime_s(result, t) == 0 ? result : NULL;
}
#define localtime_r pd_localtime_r
#define gmtime_r pd_gmtime_r
#define access _access
#define mkdir(path, mode) _mkdir(path)
#define strdup _strdup
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define strtok_r strtok_s
#define F_OK 0
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
