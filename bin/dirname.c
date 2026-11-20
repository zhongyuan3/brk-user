#include <libcmdfw.h>
#include <brk/limits.h>
#include <string.h>

static const char help[] = "Usage: dirname PATH\n"
			   "Print PATH with its last component removed.";

int main(int argc, char *argv[])
{
	char path[PATH_MAX];

	cmdfw_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		cmdfw_help_exit(help);
	if (argc != 2)
		cmdfw_usage(help);

	if (strlen(argv[1]) >= sizeof(path))
		return cmdfw_fail("path too long");
	strcpy(path, argv[1]);

	size_t len = strlen(path);
	while (len > 1 && path[len - 1] == '/')
		path[--len] = '\0';
	if (len == 0) {
		puts(".");
		return CMDFW_EXIT_OK;
	}

	char *slash = strrchr(path, '/');
	if (!slash) {
		puts(".");
		return CMDFW_EXIT_OK;
	}
	if (slash == path) {
		puts("/");
		return CMDFW_EXIT_OK;
	}

	*slash = '\0';
	len = strlen(path);
	while (len > 1 && path[len - 1] == '/')
		path[--len] = '\0';
	puts(path);
	return CMDFW_EXIT_OK;
}
