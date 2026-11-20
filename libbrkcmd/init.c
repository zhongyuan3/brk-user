#include <brk/cmd.h>

static const char *progname = "cmd";

const char *brk_cmd_basename(const char *path)
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

void brk_cmd_init(int argc, char *argv[])
{
	if (argc > 0 && argv[0])
		progname = brk_cmd_basename(argv[0]);
}

const char *brk_cmd_progname(void)
{
	return progname;
}
