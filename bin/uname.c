#include <libcmdfw.h>
#include <stdbool.h>
#include <sys/utsname.h>
#include <unistd.h>

static const char usage[] = "Usage: uname [-asnrvm]\n"
			    "Print system information.\n"
			    "\n"
			    "Options:\n"
			    "  -a       print all information\n"
			    "  -s       print the kernel name\n"
			    "  -n       print the host name\n"
			    "  -r       print the kernel release\n"
			    "  -v       print the kernel version\n"
			    "  -m       print the machine hardware name\n"
			    "  --help   display this help and exit";

static const struct cmdfw_option opts[] = {
	{ 'a', 'a', "all", CMDFW_OPT_NO_ARG },
	{ 's', 's', NULL, CMDFW_OPT_NO_ARG },
	{ 'n', 'n', NULL, CMDFW_OPT_NO_ARG },
	{ 'r', 'r', NULL, CMDFW_OPT_NO_ARG },
	{ 'v', 'v', NULL, CMDFW_OPT_NO_ARG },
	{ 'm', 'm', NULL, CMDFW_OPT_NO_ARG },
	{ CMDFW_OPT_HELP, 0, "help", CMDFW_OPT_NO_ARG },
	CMDFW_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	struct utsname u;
	bool all = false, s = false, n = false, r = false, v = false, m = false;
	bool first = true;
	int opt;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	while ((opt = cmdfw_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return CMDFW_EXIT_USAGE;
		if (opt == CMDFW_OPT_HELP)
			cmdfw_help_exit(usage);
		if (opt == 'a')
			all = true;
		if (opt == 's')
			s = true;
		if (opt == 'n')
			n = true;
		if (opt == 'r')
			r = true;
		if (opt == 'v')
			v = true;
		if (opt == 'm')
			m = true;
	}

	if (!all && !s && !n && !r && !v && !m)
		s = true;
	if (all)
		s = n = r = v = m = true;

	if (uname(&u) != 0)
		return cmdfw_fail_errno("uname failed");

#define EMIT(field)                   \
	do {                          \
		if (!first)           \
			putchar(' '); \
		fputs(field, stdout); \
		first = false;        \
	} while (0)

	if (s)
		EMIT(u.sysname);
	if (n)
		EMIT(u.nodename);
	if (r)
		EMIT(u.release);
	if (v)
		EMIT(u.version);
	if (m)
		EMIT(u.machine);
	putchar('\n');
	return CMDFW_EXIT_OK;
}
