/* desk build only: one thread, so a mutex is nothing */
#ifndef TEST_PTHREAD_H
#define TEST_PTHREAD_H
typedef int pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER 0
#define pthread_mutex_lock(m) ((void) (m))
#define pthread_mutex_unlock(m) ((void) (m))
#endif
