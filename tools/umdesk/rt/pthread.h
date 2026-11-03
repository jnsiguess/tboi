#pragma once
/*
 * desk (hotfix 1.75, jnidesk): REAL threads and REAL locks, as bionic has them - a lock taken twice by one thread is
 * not a recursive mutex there, so here it stops the run with a word instead of passing (the flag of fake/pthread.h
 * would let it by). Win32 underneath (rt.c), so this header stays free of windows.h.
 */
typedef struct { void *srw; volatile unsigned long owner; } pthread_mutex_t;
typedef struct { volatile long state; } pthread_once_t;
typedef unsigned long long pthread_t;
#define PTHREAD_MUTEX_INITIALIZER { 0, 0 }
#define PTHREAD_ONCE_INIT { 0 }
int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_unlock(pthread_mutex_t *m);
int pthread_mutex_trylock(pthread_mutex_t *m);
int pthread_once(pthread_once_t *o, void (*f)(void));
int pthread_create(pthread_t *t, const void *attr, void *(*f)(void *), void *arg);
int pthread_detach(pthread_t t);
