#include <brk/cmd.h>
#include <brk/limits.h>
#include <string.h>

static const char help[] = "Usage: basename PATH [SUFFIX]\n"
			   "Print PATH with its directory components removed.\n"
			   "If SUFFIX is given, remove it from the result.";

int main(int argc, char *argv[])
{
	char path[PATH_MAX];

	brk_cmd_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(help);
	if (argc < 2 || argc > 3)
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

	char *base = strrchr(path, '/');
	base = base ? base + 1 : path;
	if (!base[0]) {
		puts("/");
		return BRK_CMD_EXIT_OK;
	}

	if (argc == 3) {
		size_t blen = strlen(base);
		size_t slen = strlen(argv[2]);

		if (blen > slen && !strcmp(base + blen - slen, argv[2]))
			base[blen - slen] = '\0';
	}

	puts(base);
	return BRK_CMD_EXIT_OK;
}
