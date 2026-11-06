#include <brk/cmd.h>
#include <signal.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static const char usage[] =
	"Usage: kill [-SIGNAL] PID...\n"
	"       kill -l [SIGNAL]\n"
	"Send a signal (default: TERM) to each PID.\n"
	"SIGNAL may be a number or a name such as TERM, KILL or USR1.";

struct sig_name {
	const char *name;
	int num;
};

static const struct sig_name signals[] = {
	{ "HUP", SIGHUP },   { "INT", SIGINT },	    { "QUIT", SIGQUIT },
	{ "ILL", SIGILL },   { "TRAP", SIGTRAP },   { "ABRT", SIGABRT },
	{ "BUS", SIGBUS },   { "FPE", SIGFPE },	    { "KILL", SIGKILL },
	{ "USR1", SIGUSR1 }, { "SEGV", SIGSEGV },   { "USR2", SIGUSR2 },
	{ "PIPE", SIGPIPE }, { "ALRM", SIGALRM },   { "TERM", SIGTERM },
	{ "CHLD", SIGCHLD }, { "CONT", SIGCONT },   { "STOP", SIGSTOP },
	{ "TSTP", SIGTSTP }, { "WINCH", SIGWINCH },
};

static int parse_signal_name(const char *s)
{
	for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
		if (!strcasecmp(s, signals[i].name))
			return signals[i].num;
	}
	return -1;
}

static const char *signal_name(int num)
{
	for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++) {
		if (signals[i].num == num)
			return signals[i].name;
	}
	return NULL;
}

static bool all_digits(const char *s)
{
	if (!*s)
		return false;
	for (; *s; s++) {
		if (*s < '0' || *s > '9')
			return false;
	}
	return true;
}

static void list_signals(void)
{
	for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); i++)
		printf("%2d) SIG%s\n", signals[i].num, signals[i].name);
}

int main(int argc, char *argv[])
{
	int sig = SIGTERM;
	int arg = 1;

	brk_cmd_init(argc, argv);

	if (arg < argc && !strcmp(argv[arg], "-l")) {
		arg++;
		if (arg < argc) {
			if (all_digits(argv[arg])) {
				long n;

				if (brk_cmd_parse_long(argv[arg], &n, 0,
						       NSIG - 1) != 0)
					return BRK_CMD_EXIT_FAIL;
				const char *nm = signal_name((int)n);

				if (nm)
					printf("%s\n", nm);
				else
					printf("%ld\n", n);
			} else {
				int num = parse_signal_name(argv[arg]);

				if (num < 0)
					return brk_cmd_fail(
						"unknown signal '%s'",
						argv[arg]);
				printf("%d\n", num);
			}
		} else {
			list_signals();
		}
		return BRK_CMD_EXIT_OK;
	}

	if (arg < argc && argv[arg][0] == '-' && argv[arg][1]) {
		const char *s = argv[arg] + 1;
		long n;
		int named = parse_signal_name(s);

		if (named > 0) {
			sig = named;
		} else if (brk_cmd_parse_long(s, &n, 0, NSIG - 1) == 0) {
			sig = (int)n;
		} else {
			return BRK_CMD_EXIT_FAIL;
		}
		arg++;
	}

	if (arg >= argc)
		brk_cmd_usage(usage);

	for (; arg < argc; arg++) {
		pid_t pid;

		if (brk_cmd_parse_pid(argv[arg], &pid) != 0)
			return BRK_CMD_EXIT_FAIL;
		if (kill(pid, sig) != 0)
			return brk_cmd_fail_errno("kill failed");
	}

	return BRK_CMD_EXIT_OK;
}
