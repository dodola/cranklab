#ifndef PD_GEOMETRY_H
#define PD_GEOMETRY_H

#include "pd_runtime.h"

typedef struct {
    float x, y;
} PDPoint;

typedef struct {
    float x, y, width, height;
} PDRectInternal;

typedef struct {
    float width, height;
} PDSize;

typedef struct {
    int count;
    float *coords;
} Polygon;

typedef struct {
    float m[6];
} AffineTransform;

void pd_geometry_register(lua_State *L);
void pd_push_vector2D(lua_State *L, float dx, float dy);
void pd_push_rect(lua_State *L, float x, float y, float w, float h);
void pd_push_point(lua_State *L, float x, float y);
int pd_get_lineSegment(lua_State *L, int idx, float *x1, float *y1, float *x2, float *y2);

#endif
