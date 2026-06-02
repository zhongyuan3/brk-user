#include <apputil.h>

static const char *progname = "app";

const char *app_basename(const char *path)
{
	const char *base = path;

	if (!path)
		return "app";

	for (const char *p = path; *p; p++) {
		if (*p == '/')
			base = p + 1;
	}

	return base[0] ? base : path;
}

void app_init(int argc, char *argv[])
{
	if (argc > 0 && argv[0])
		progname = app_basename(argv[0]);
}

const char *app_progname(void)
{
	return progname;
}
