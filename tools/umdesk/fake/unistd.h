#pragma once
#include <io.h>
#include <process.h>
static int gettid(void) { return 1; }
#define getpid _getpid
static unsigned sleep(unsigned s) { (void) s; return 0; }
