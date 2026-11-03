/* the desk's stand-in for Android's log: printed, not sent anywhere */
#pragma once
#include <stdio.h>
#define ANDROID_LOG_INFO 4
#define __android_log_print(prio, tag, ...) (printf("  [%s] ", tag), printf(__VA_ARGS__), printf("\n"))
