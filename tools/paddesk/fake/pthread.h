/* the desk runs one thread: the mutex is a counter that must balance */
#pragma once
typedef struct { int depth; } pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER { 0 }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { m->depth++; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { m->depth--; return 0; }
