#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

static void (*__brk_handlers[NSIG])(int);

struct k_sigaction {
	unsigned long sa_handler;
	sigset_t sa_mask;
	unsigned long sa_flags;
};

static struct k_sigaction user_to_k(const struct sigaction *act)
{
	struct k_sigaction kact;

	kact.sa_handler = (unsigned long)act->sa_handler;
	kact.sa_mask = act->sa_mask;
	kact.sa_flags = act->sa_flags;
	return kact;
}

static void k_to_user(const struct k_sigaction *kact, struct sigaction *act)
{
	act->sa_handler = (void (*)(int))kact->sa_handler;
	act->sa_mask = kact->sa_mask;
	act->sa_flags = kact->sa_flags;
}

void __brk_signal_dispatch(int sig)
{
	void (*handler)(int) = __brk_handlers[sig];

	if (handler)
		handler(sig);
	sigreturn();
}

int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
	struct k_sigaction kact;
	struct k_sigaction kold;
	const struct k_sigaction *pass = act ? &kact : NULL;

	if (signum <= 0 || signum >= NSIG)
		return -EINVAL;

	if (act) {
		if (act->sa_handler == SIG_DFL || act->sa_handler == SIG_IGN) {
			__brk_handlers[signum] = NULL;
			kact = user_to_k(act);
		} else {
			__brk_handlers[signum] = act->sa_handler;
			kact.sa_handler = (unsigned long)__brk_signal_dispatch;
			kact.sa_mask = act->sa_mask;
			kact.sa_flags = act->sa_flags;
		}
	}

	if (syscall(SYS_rt_sigaction, signum, pass, oldact ? &kold : NULL,
		    sizeof(sigset_t)) != 0)
		return -1;

	if (oldact)
		k_to_user(&kold, oldact);
	return 0;
}

int sigprocmask(int how, const sigset_t *set, sigset_t *oldset)
{
	return syscall(SYS_rt_sigprocmask, how, set, oldset, sizeof(sigset_t));
}

int sigreturn(void)
{
	return syscall(SYS_rt_sigreturn);
}
