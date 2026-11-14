#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/time.h>
#include <sys/types.h>
#include <uapi/signal.h>

int kill(pid_t pid, int sig);
int raise(int sig);
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
