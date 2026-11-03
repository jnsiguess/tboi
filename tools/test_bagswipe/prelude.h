/* desk build only, forced in before items.c: a clock the test turns by hand */
#ifndef TEST_PRELUDE_H
#define TEST_PRELUDE_H
#include <time.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
int fake_clock_gettime(int clock, struct timespec *ts);
#define clock_gettime fake_clock_gettime
#endif
