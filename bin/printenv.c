#include <libcmdfw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: printenv [NAME]...\n"
			    "Print environment variables.";

int main(int argc, char *argv[])
{
	cmdfw_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		cmdfw_help_exit(usage);

	if (argc == 1) {
		for (char **entry = environ; entry && *entry; entry++)
			puts(*entry);
		return CMDFW_EXIT_OK;
	}

	int status = CMDFW_EXIT_OK;

	for (int i = 1; i < argc; i++) {
		const char *value = getenv(argv[i]);

		if (value)
			puts(value);
		else
			status = CMDFW_EXIT_FAIL;
	}
	return status;
}
