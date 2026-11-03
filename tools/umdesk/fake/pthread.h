#pragma once
/* desk: one thread; a lock is a flag, so a trylock inside a held lock answers "busy" as on the phone */
typedef int pthread_mutex_t;
typedef int pthread_once_t;
#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_ONCE_INIT 0
static int pthread_mutex_lock(pthread_mutex_t *m) { *m = 1; return 0; }
static int pthread_mutex_unlock(pthread_mutex_t *m) { *m = 0; return 0; }
static int pthread_mutex_trylock(pthread_mutex_t *m) { if (*m) return 16; *m = 1; return 0; }
static int pthread_once(pthread_once_t *o, void (*f)(void)) { if (!*o) { *o = 1; f(); } return 0; }
/* a thread of the app's own (usermods.c THE PAIRS): on the desk it runs at once, to its end, so a run is one answer */
typedef int pthread_t;
static int pthread_create(pthread_t *t, const void *attr, void *(*f)(void *), void *arg) { (void) attr; *t = 1; f(arg); return 0; }
static int pthread_detach(pthread_t t) { (void) t; return 0; }
