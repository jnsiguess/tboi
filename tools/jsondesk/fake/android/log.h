#pragma once
/* the desk's logcat: what __android_log_print says goes to stdout, tagged as on the phone */
#include <stdio.h>
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_WARN 5
#define ANDROID_LOG_ERROR 6
#define __android_log_print(prio, tag, ...) (printf("%s: ", tag), printf(__VA_ARGS__), putchar('\n'), 0)
