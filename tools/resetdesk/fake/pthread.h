/* the reset desk: one thread, so the lock is nothing (tools/resetdesk) */
#pragma once
typedef int pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER 0
static inline int pthread_mutex_lock(pthread_mutex_t *m) { (void) m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { (void) m; return 0; }
