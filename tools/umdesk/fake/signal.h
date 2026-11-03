#pragma once
#define SIGSEGV 11
#define SIGBUS 7
#define SIGILL 4
#define SIGFPE 8
#define SIGABRT 6
#define SA_SIGINFO 4
typedef struct { int si_signo, si_code; void *si_addr; } siginfo_t;
typedef int sigset_t;
typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t) 0)
#define SIG_IGN ((sighandler_t) 1)
struct sigaction { void (*sa_sigaction)(int, siginfo_t *, void *); sighandler_t sa_handler; int sa_flags; sigset_t sa_mask; };
static int sigemptyset(sigset_t *s) { *s = 0; return 0; }
static int sigaction(int s, const struct sigaction *a, struct sigaction *o) { (void) s; (void) a; if (o) { o->sa_flags = 0; o->sa_handler = SIG_DFL; o->sa_sigaction = 0; } return 0; }
static sighandler_t signal(int s, sighandler_t h) { (void) s; (void) h; return 0; }
static int raise(int s) { (void) s; return 0; }
