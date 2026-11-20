#include <libcmdfw.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: rmdir DIR...\n"
			    "Remove empty directories.";

int main(int argc, char *argv[])
{
	int ret = CMDFW_EXIT_OK;

	cmdfw_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		cmdfw_help_exit(usage);
	if (argc < 2)
		cmdfw_usage(usage);

	for (int i = 1; i < argc; i++) {
		if (rmdir(argv[i]) != 0)
			ret = cmdfw_fail_errno(argv[i]);
	}
	return ret;
}
