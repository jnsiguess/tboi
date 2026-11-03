#pragma once
/* desk: "libz.so" is Git for Windows' zlib1.dll (win.c) - the same inflate the phone's libz.so is */
#define RTLD_NOW 2
void *dlopen(const char *name, int flags);
void *dlsym(void *h, const char *name);
