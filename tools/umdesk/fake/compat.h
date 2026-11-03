/* desk harness: just enough POSIX for usermods.c to compile on Windows (LLVM clang + MSVC CRT) */
#pragma once
#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <io.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
typedef long long ssize_t;
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define lstat stat
#define mkdir(p, m) _mkdir(p)
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define __thread __declspec(thread)
#define CLOCK_MONOTONIC 1
static int clock_gettime(int c, struct timespec *t) { (void) c; timespec_get(t, TIME_UTC); return 0; }
static void *memmem(const void *h, size_t hn, const void *n, size_t nn) {
    const char *a = (const char *) h;
    if (!nn) return (void *) h;
    for (size_t i = 0; i + nn <= hn; i++) if (!memcmp(a + i, n, nn)) return (void *) (a + i);
    return NULL;
}
