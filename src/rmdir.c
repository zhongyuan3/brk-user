#include <apputil.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: rmdir DIR...\n"
			    "Remove empty directories.";

int main(int argc, char *argv[])
{
	int ret = APP_EXIT_OK;

	app_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		app_help_exit(usage);
	if (argc < 2)
		app_usage(usage);

	for (int i = 1; i < argc; i++) {
		if (rmdir(argv[i]) != 0)
			ret = app_fail_errno(argv[i]);
	}
	return ret;
}
