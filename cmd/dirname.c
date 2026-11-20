#include <brk/cmd.h>
#include <brk/limits.h>
#include <string.h>

static const char help[] = "Usage: dirname PATH\n"
			   "Print PATH with its last component removed.";

int main(int argc, char *argv[])
{
	char path[PATH_MAX];

	brk_cmd_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(help);
	if (argc != 2)
		brk_cmd_usage(help);

	if (strlen(argv[1]) >= sizeof(path))
		return brk_cmd_fail("path too long");
	strcpy(path, argv[1]);

	size_t len = strlen(path);
	while (len > 1 && path[len - 1] == '/')
		path[--len] = '\0';
	if (len == 0) {
		puts(".");
		return BRK_CMD_EXIT_OK;
	}

	char *slash = strrchr(path, '/');
	if (!slash) {
		puts(".");
		return BRK_CMD_EXIT_OK;
	}
	if (slash == path) {
		puts("/");
		return BRK_CMD_EXIT_OK;
	}

	*slash = '\0';
	len = strlen(path);
	while (len > 1 && path[len - 1] == '/')
		path[--len] = '\0';
	puts(path);
	return BRK_CMD_EXIT_OK;
}
