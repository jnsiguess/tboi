/* the reset desk: just enough POSIX for reset.c on Windows (tools/resetdesk) */
#ifndef DESK_PRELUDE_H
#define DESK_PRELUDE_H
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
int desk_clock_gettime(int clock, struct timespec *ts);
#define clock_gettime desk_clock_gettime
#endif
