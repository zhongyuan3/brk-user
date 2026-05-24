#ifndef _SIGNAL_H
#define _SIGNAL_H

#include <sys/types.h>

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGILL 4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGWINCH 28

#define NSIG 32

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int)) - 1)

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SA_NOCLDSTOP 0x00000001
#define SA_RESTART 0x10000000
#define SA_NODEFER 0x40000000

typedef unsigned long sigset_t;

struct sigaction {
	void (*sa_handler)(int);
	sigset_t sa_mask;
	unsigned long sa_flags;
};

int kill(pid_t pid, int sig);
int raise(int sig);
void (*signal(int signum, void (*handler)(int)))(int);
int sigaction(int signum, const struct sigaction *act,
	      struct sigaction *oldact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
int sigreturn(void);

void __brk_signal_dispatch(int sig);

#endif
