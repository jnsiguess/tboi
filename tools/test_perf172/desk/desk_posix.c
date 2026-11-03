/* desk build only: the implementation behind desk_posix.h, pthread.h and dirent.h */
#include <stdarg.h>
#include <process.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "pthread.h"
#include "dirent.h"
#undef open
#undef rename
#undef fsync
#undef mkdir
#include <direct.h>

volatile int g_desk_fsync_ms;
char g_desk_rename_refuse[260];
volatile long g_desk_renames;

int desk_open(const char *path, int flags, ...) {
    int mode = 0;
    if (flags & _O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    (void) mode;
    return _open(path, flags | _O_BINARY, _S_IREAD | _S_IWRITE);
}

static int ends(const char *s, const char *t) {
    size_t a = strlen(s), b = strlen(t);
    return b && a >= b && !strcmp(s + a - b, t);
}

int desk_rename(const char *from, const char *to) {
    if (g_desk_rename_refuse[0] && ends(to, g_desk_rename_refuse)) { errno = EACCES; return -1; }
    InterlockedIncrement(&g_desk_renames);
    /* POSIX renames over a file somebody is reading; Windows refuses while it is open: wait for the reader */
    for (int i = 0; i < 400; i++) {
        if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 0;
        DWORD e = GetLastError();
        if (e != ERROR_SHARING_VIOLATION && e != ERROR_ACCESS_DENIED) break;
        Sleep(1);
    }
    errno = GetLastError() == ERROR_FILE_NOT_FOUND ? ENOENT : EACCES;
    return -1;
}

int desk_fsync(int fd) {
    if (g_desk_fsync_ms > 0) Sleep((DWORD) g_desk_fsync_ms);
    return _commit(fd);
}

int desk_mkdir(const char *path, int mode) { (void) mode; return _mkdir(path); }

static LARGE_INTEGER g_freq;
volatile long long g_desk_skew_ns;              /* a test moves the monotonic clock forward */
int desk_clock_gettime(int clock, struct timespec *ts) {
    if (clock == CLOCK_REALTIME) { timespec_get(ts, TIME_UTC); return 0; }
    if (!g_freq.QuadPart) QueryPerformanceFrequency(&g_freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    long long ns = (long long) ((double) c.QuadPart * 1e9 / (double) g_freq.QuadPart) + g_desk_skew_ns;
    ts->tv_sec = (time_t) (ns / 1000000000LL);
    ts->tv_nsec = (long) (ns % 1000000000LL);
    return 0;
}

int desk_nanosleep(const struct timespec *req, struct timespec *rem) {
    (void) rem;
    long long ms = (long long) req->tv_sec * 1000 + req->tv_nsec / 1000000;
    if (ms <= 0 && req->tv_nsec > 0) ms = 1;
    Sleep((DWORD) ms);
    return 0;
}

int usleep(unsigned us) { Sleep(us / 1000 ? us / 1000 : 1); return 0; }
int gettid(void) { return (int) GetCurrentThreadId(); }

/* ---- pthreads ---- */
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void) a; InitializeSRWLock((PSRWLOCK) m); return 0; }
int pthread_mutex_lock(pthread_mutex_t *m) { AcquireSRWLockExclusive((PSRWLOCK) m); return 0; }
int pthread_mutex_trylock(pthread_mutex_t *m) { return TryAcquireSRWLockExclusive((PSRWLOCK) m) ? 0 : EBUSY; }
int pthread_mutex_unlock(pthread_mutex_t *m) { ReleaseSRWLockExclusive((PSRWLOCK) m); return 0; }
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) {
    SleepConditionVariableSRW((PCONDITION_VARIABLE) c, (PSRWLOCK) m, INFINITE, 0);
    return 0;
}
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abs) {
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    long long ms = ((long long) abs->tv_sec - now.tv_sec) * 1000 + (abs->tv_nsec - now.tv_nsec) / 1000000;
    if (ms <= 0) return ETIMEDOUT;
    if (!SleepConditionVariableSRW((PCONDITION_VARIABLE) c, (PSRWLOCK) m, (DWORD) ms, 0))
        return GetLastError() == ERROR_TIMEOUT ? ETIMEDOUT : EINVAL;
    return 0;
}
int pthread_cond_signal(pthread_cond_t *c) { WakeConditionVariable((PCONDITION_VARIABLE) c); return 0; }
int pthread_cond_broadcast(pthread_cond_t *c) { WakeAllConditionVariable((PCONDITION_VARIABLE) c); return 0; }
int pthread_attr_init(pthread_attr_t *a) { a->detached = 0; return 0; }
int pthread_attr_destroy(pthread_attr_t *a) { (void) a; return 0; }
int pthread_attr_setdetachstate(pthread_attr_t *a, int d) { a->detached = d; return 0; }

typedef struct { void *(*fn)(void *); void *arg; } start_t;
static unsigned __stdcall trampoline(void *p) {
    start_t s = *(start_t *) p;
    free(p);
    s.fn(s.arg);
    return 0;
}
#define MAX_JOIN 256
static struct { unsigned long id; HANDLE h; } g_joinable[MAX_JOIN];
static SRWLOCK g_join_lock = SRWLOCK_INIT;
int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg) {
    start_t *s = (start_t *) malloc(sizeof(*s));
    if (!s) return EAGAIN;
    s->fn = fn;
    s->arg = arg;
    unsigned id = 0;
    HANDLE h = (HANDLE) _beginthreadex(NULL, 0, trampoline, s, 0, &id);
    if (!h) { free(s); return EAGAIN; }
    *t = id;
    if (a && a->detached) { CloseHandle(h); return 0; }
    AcquireSRWLockExclusive(&g_join_lock);
    for (int i = 0; i < MAX_JOIN; i++) if (!g_joinable[i].h) { g_joinable[i].id = id; g_joinable[i].h = h; break; }
    ReleaseSRWLockExclusive(&g_join_lock);
    return 0;
}
int pthread_join(pthread_t t, void **ret) {
    if (ret) *ret = NULL;
    HANDLE h = NULL;
    AcquireSRWLockExclusive(&g_join_lock);
    for (int i = 0; i < MAX_JOIN; i++) if (g_joinable[i].h && g_joinable[i].id == t) { h = g_joinable[i].h; g_joinable[i].h = NULL; }
    ReleaseSRWLockExclusive(&g_join_lock);
    if (!h) return EINVAL;
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    return 0;
}
pthread_t pthread_self(void) { return GetCurrentThreadId(); }
int pthread_equal(pthread_t a, pthread_t b) { return a == b; }

/* ---- directories ---- */
struct desk_dir { HANDLE h; WIN32_FIND_DATAA fd; int first, done; char pattern[300]; struct dirent e; };
DIR *opendir(const char *path) {
    DIR *d = (DIR *) calloc(1, sizeof(*d));
    if (!d) return NULL;
    snprintf(d->pattern, sizeof(d->pattern), "%s\\*", path);
    d->h = FindFirstFileA(d->pattern, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE) { free(d); errno = ENOENT; return NULL; }
    d->first = 1;
    return d;
}
struct dirent *readdir(DIR *d) {
    if (d->done) return NULL;
    if (!d->first && !FindNextFileA(d->h, &d->fd)) { d->done = 1; return NULL; }
    d->first = 0;
    snprintf(d->e.d_name, sizeof(d->e.d_name), "%s", d->fd.cFileName);
    d->e.d_type = (d->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 4 : 8;
    return &d->e;
}
void rewinddir(DIR *d) {
    FindClose(d->h);
    d->h = FindFirstFileA(d->pattern, &d->fd);
    d->first = 1;
    d->done = d->h == INVALID_HANDLE_VALUE;
}
int closedir(DIR *d) { if (d->h != INVALID_HANDLE_VALUE) FindClose(d->h); free(d); return 0; }
