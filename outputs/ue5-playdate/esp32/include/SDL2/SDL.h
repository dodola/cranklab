#pragma once
/* The shared software rasterizer only needs these opaque desktop handles.
   No SDL surfaces or RGBA framebuffer are allocated on the microcontroller. */
#include <stdint.h>
#include <stddef.h>
#define SDL_VERSION_ATLEAST(a,b,c) 1
#define SDL_PIXELFORMAT_ARGB8888 0
#define SDL_TEXTUREACCESS_STREAMING 0
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Window SDL_Window;
static inline void SDL_DestroyTexture(SDL_Texture *p) { (void)p; }
static inline SDL_Texture *SDL_CreateTexture(SDL_Renderer *r,int f,int a,int w,int h) { return NULL; }
static inline int SDL_LockTexture(SDL_Texture *t,void *rect,void **p,int *pitch) { return -1; }
static inline void SDL_UnlockTexture(SDL_Texture *t) { (void)t; }
static inline void SDL_RenderCopy(SDL_Renderer *r,SDL_Texture *t,void *src,void *dst) {}
static inline void SDL_RenderPresent(SDL_Renderer *r) { (void)r; }
