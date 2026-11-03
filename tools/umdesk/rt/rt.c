/* the real threads of rt/pthread.h (hotfix 1.75, jnidesk): SRW locks and CreateThread */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "pthread.h"

int pthread_mutex_lock(pthread_mutex_t *m) {
    DWORD me = GetCurrentThreadId();
    if (m->owner == me) {
        fprintf(stderr, "RECURSIVE LOCK of %p by thread %lu - bionic would hang here\n", (void *) m, (unsigned long) me);
        fflush(stderr);
        abort();
    }
    AcquireSRWLockExclusive((PSRWLOCK) &m->srw);
    m->owner = me;
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m) {
    m->owner = 0;
    ReleaseSRWLockExclusive((PSRWLOCK) &m->srw);
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m) {
    if (m->owner == GetCurrentThreadId()) return 16;
    if (!TryAcquireSRWLockExclusive((PSRWLOCK) &m->srw)) return 16;
    m->owner = GetCurrentThreadId();
    return 0;
}

int pthread_once(pthread_once_t *o, void (*f)(void)) {
    if (InterlockedCompareExchange(&o->state, 1, 0) == 0) { f(); InterlockedExchange(&o->state, 2); return 0; }
    while (o->state != 2) Sleep(0);
    return 0;
}

typedef struct { void *(*f)(void *); void *arg; } start_t;
static DWORD WINAPI trampoline(LPVOID p) {
    start_t s = *(start_t *) p;
    free(p);
    s.f(s.arg);
    return 0;
}

int pthread_create(pthread_t *t, const void *attr, void *(*f)(void *), void *arg) {
    (void) attr;
    start_t *s = (start_t *) malloc(sizeof(*s));
    if (!s) return 12;
    s->f = f;
    s->arg = arg;
    HANDLE h = CreateThread(NULL, 1 << 20, trampoline, s, 0, NULL);
    if (!h) { free(s); return 11; }
    *t = (pthread_t) (unsigned long long) h;
    return 0;
}

int pthread_detach(pthread_t t) { CloseHandle((HANDLE) (unsigned long long) t); return 0; }
