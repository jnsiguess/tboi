/* what Windows' C runtime lacks and pad.c asks for: the clock the desk drives */
#pragma once
#include <time.h>
#define CLOCK_MONOTONIC 1
extern long long g_desk_ms;
static inline int clock_gettime(int clk, struct timespec *t) {
    (void) clk;
    t->tv_sec = (time_t) (g_desk_ms / 1000);
    t->tv_nsec = (long) ((g_desk_ms % 1000) * 1000000);
    return 0;
}
