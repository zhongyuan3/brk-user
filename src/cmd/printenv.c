#include <brk/cmd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: printenv [NAME]...\n"
			    "Print environment variables.";

int main(int argc, char *argv[])
{
	brk_cmd_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(usage);

	if (argc == 1) {
		for (char **entry = environ; entry && *entry; entry++)
			puts(*entry);
		return BRK_CMD_EXIT_OK;
	}

	int status = BRK_CMD_EXIT_OK;

	for (int i = 1; i < argc; i++) {
		const char *value = getenv(argv[i]);

		if (value)
			puts(value);
		else
			status = BRK_CMD_EXIT_FAIL;
	}
	return status;
}
