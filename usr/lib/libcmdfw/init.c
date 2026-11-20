#include <libcmdfw.h>

static const char *progname = "cmd";

const char *cmdfw_basename(const char *path)
{
	const char *base = path;

	if (!path)
		return "cmd";

	for (const char *p = path; *p; p++) {
		if (*p == '/')
			base = p + 1;
	}

	return base[0] ? base : path;
}

void cmdfw_init(int argc, char *argv[])
{
	if (argc > 0 && argv[0])
		progname = cmdfw_basename(argv[0]);
}

const char *cmdfw_progname(void)
{
	return progname;
}
