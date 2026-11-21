#include <signal.h>
#include <stdio.h>

/*
 * Names for the signals BRK defines.  Entries past the table (and
 * the real-time slots) render as "Signal N".
 */
static const char *const signames[NSIG] = {
	[0] = "Unknown signal",
	[SIGHUP] = "Hangup",
	[SIGINT] = "Interrupt",
	[SIGQUIT] = "Quit",
	[SIGILL] = "Illegal instruction",
	[SIGTRAP] = "Trace/breakpoint trap",
	[SIGABRT] = "Aborted",
	[SIGBUS] = "Bus error",
	[SIGFPE] = "Floating point exception",
	[SIGKILL] = "Killed",
	[SIGUSR1] = "User defined signal 1",
	[SIGSEGV] = "Segmentation fault",
	[SIGUSR2] = "User defined signal 2",
	[SIGPIPE] = "Broken pipe",
	[SIGALRM] = "Alarm clock",
	[SIGTERM] = "Terminated",
	[SIGCHLD] = "Child exited",
	[SIGCONT] = "Continued",
	[SIGSTOP] = "Stopped (signal)",
	[SIGTSTP] = "Stopped",
	[SIGTTIN] = "Stopped (tty input)",
	[SIGTTOU] = "Stopped (tty output)",
	[SIGURG] = "Urgent I/O condition",
	[SIGWINCH] = "Window changed",
};

const char *strsignal(int signum)
{
	static char buf[32];

	if (signum <= 0 || signum >= NSIG) {
		snprintf(buf, sizeof(buf), "Signal %d", signum);
		return buf;
	}
	return signames[signum];
}
