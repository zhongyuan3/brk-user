#include <brk/cmd.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: rmdir DIR...\n"
			    "Remove empty directories.";

int main(int argc, char *argv[])
{
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(usage);
	if (argc < 2)
		brk_cmd_usage(usage);

	for (int i = 1; i < argc; i++) {
		if (rmdir(argv[i]) != 0)
			ret = brk_cmd_fail_errno(argv[i]);
	}
	return ret;
}
