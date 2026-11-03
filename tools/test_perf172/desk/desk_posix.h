/*
 * Desk build only (hotfix 1.72 tests): the POSIX the port's files expect, on
 * Windows with the MSVC runtime. Forced in before every file (-include).
 * Real threads, real files, real time - the point is to run the port's own
 * code as it is, not a model of it.
 */
#ifndef DESK_POSIX_H
#define DESK_POSIX_H
#define DESK_TEST 1
#define _CRT_DECLARE_NONSTDC_NAMES 1
#define _CRT_SECURE_NO_WARNINGS 1
#define _CRT_NONSTDC_NO_WARNINGS 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <io.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>

typedef long long ssize_t_desk;
#define ssize_t ssize_t_desk
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef F_OK
#define F_OK 0
#define W_OK 2
#define R_OK 4
#endif

/* files: binary always, rename over an existing file as POSIX does, fsync = _commit */
int desk_open(const char *path, int flags, ...);
int desk_rename(const char *from, const char *to);
int desk_fsync(int fd);
int desk_mkdir(const char *path, int mode);
#define open desk_open
#define rename desk_rename
#define fsync desk_fsync
#define mkdir desk_mkdir

/* the faults a test can ask for */
extern volatile int g_desk_fsync_ms;            /* every fsync takes this long */
extern char g_desk_rename_refuse[260];          /* rename onto a path ending in this fails (EACCES) */
extern volatile long g_desk_renames;
extern volatile long long g_desk_skew_ns;       /* added to CLOCK_MONOTONIC */

/* time */
#ifndef CLOCK_MONOTONIC
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#endif
int desk_clock_gettime(int clock, struct timespec *ts);
int desk_nanosleep(const struct timespec *req, struct timespec *rem);
#define clock_gettime desk_clock_gettime
#define nanosleep desk_nanosleep
int usleep(unsigned us);
int gettid(void);

/* bionic's funopen does not exist here: safefile.c takes a tmpfile under DESK_TEST */
typedef long long fpos_t_desk;
#endif
