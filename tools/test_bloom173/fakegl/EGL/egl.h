/* A desk stand-in for <EGL/egl.h>: what gpu.c's pass clock asks (tools/test_bloom173). */
#ifndef FAKE_EGL_H
#define FAKE_EGL_H
typedef int EGLint;
typedef void *EGLDisplay;
typedef void *EGLSurface;
#define EGL_NO_DISPLAY ((EGLDisplay) 0)
#define EGL_NO_SURFACE ((EGLSurface) 0)
#define EGL_DRAW 0x3059
#define EGL_SWAP_BEHAVIOR 0x3093
#define EGL_RENDER_BUFFER 0x3086
#define EGL_BUFFER_PRESERVED 0x3094
#define EGL_BUFFER_DESTROYED 0x3095
#define EGL_BACK_BUFFER 0x3084
#define EGL_SINGLE_BUFFER 0x3085
void *eglGetProcAddress(const char *);
EGLDisplay eglGetCurrentDisplay(void);
EGLSurface eglGetCurrentSurface(EGLint);
unsigned eglQuerySurface(EGLDisplay, EGLSurface, EGLint, EGLint *);
#endif
