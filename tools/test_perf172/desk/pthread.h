/* desk build only: pthreads over Win32 SRW locks, condition variables and threads */
#ifndef DESK_PTHREAD_H
#define DESK_PTHREAD_H
#include <time.h>
typedef struct { void *p; } pthread_mutex_t;          /* an SRWLOCK */
typedef struct { void *p; } pthread_cond_t;           /* a CONDITION_VARIABLE */
typedef unsigned long pthread_t;                      /* the thread id */
typedef struct { int detached; } pthread_attr_t;
typedef int pthread_mutexattr_t;
#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_COND_INITIALIZER { 0 }
#define PTHREAD_CREATE_DETACHED 1
#define PTHREAD_CREATE_JOINABLE 0
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a);
int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_trylock(pthread_mutex_t *m);
int pthread_mutex_unlock(pthread_mutex_t *m);
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abs_realtime);
int pthread_cond_signal(pthread_cond_t *c);
int pthread_cond_broadcast(pthread_cond_t *c);
int pthread_attr_init(pthread_attr_t *a);
int pthread_attr_destroy(pthread_attr_t *a);
int pthread_attr_setdetachstate(pthread_attr_t *a, int d);
int pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg);
int pthread_join(pthread_t t, void **ret);
pthread_t pthread_self(void);
int pthread_equal(pthread_t a, pthread_t b);
#define pthread_setname_np(t, n) ((void) (t), (void) (n), 0)
#ifndef ETIMEDOUT
#define ETIMEDOUT 138
#endif
#ifndef EBUSY
#define EBUSY 16
#endif
#endif
