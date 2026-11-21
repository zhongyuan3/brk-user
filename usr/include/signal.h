#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>
#include <time.h>
#include <uapi/signal.h>

/*
 * POSIX signal surface, libc-owned.  sigset_t and siginfo_t alias the
 * kernel ABI types (__brk_sigset_t / __brk_siginfo_t) since their
 * shapes cross the syscall boundary unchanged; struct sigaction and
 * union sigval are defined here and pinned to the kernel layout by
 * abi_check.c.
 */
typedef __brk_sigset_t sigset_t;
typedef int sig_atomic_t;

struct sigaction {
	void (*sa_handler)(int);
	sigset_t sa_mask;
	unsigned long sa_flags;
};

union sigval {
	int sival_int;
	void *sival_ptr;
};

typedef union sigval sigval_t;
typedef __brk_siginfo_t siginfo_t;

int kill(pid_t pid, int sig);
int raise(int signum);
void (*signal(int signum, void (*handler)(int)))(int);
int sigaction(int signum, const struct sigaction *act,
	      struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigpending(sigset_t *set);
int sigtimedwait(const sigset_t *set, const struct timespec *timeout,
		 siginfo_t *info);
int sigwaitinfo(const sigset_t *set, siginfo_t *info);
int sigwait(const sigset_t *set, int *sig);
int sigsuspend(const sigset_t *mask);
int sigreturn(void);

void __brk_signal_dispatch(int sig);

#endif
