#include <brk/cmd.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] = "Usage: test EXPR\n"
			    "       [ EXPR ]\n"
			    "Evaluate a file, string or integer expression.\n"
			    "\n"
			    "Unary:  -e -f -d -r -w -x -s -z -n\n"
			    "Binary: = != -eq -ne -lt -le -gt -ge";

static int eval(int argc, char **argv);

static int eval_unary(const char *op, const char *arg)
{
	struct stat st;

	if (!strcmp(op, "-z"))
		return arg[0] == '\0';
	if (!strcmp(op, "-n"))
		return arg[0] != '\0';
	if (!strcmp(op, "-r"))
		return access(arg, R_OK) == 0;
	if (!strcmp(op, "-w"))
		return access(arg, W_OK) == 0;
	if (!strcmp(op, "-x"))
		return access(arg, X_OK) == 0;

	if (stat(arg, &st) != 0)
		return 0;

	if (!strcmp(op, "-e"))
		return 1;
	if (!strcmp(op, "-f"))
		return S_ISREG(st.st_mode);
	if (!strcmp(op, "-d"))
		return S_ISDIR(st.st_mode);
	if (!strcmp(op, "-s"))
		return st.st_size > 0;
	return 0;
}

static int eval_binary(const char *a, const char *op, const char *b)
{
	if (!strcmp(op, "="))
		return strcmp(a, b) == 0;
	if (!strcmp(op, "!="))
		return strcmp(a, b) != 0;

	char *enda, *endb;
	long x = strtol(a, &enda, 10);
	long y = strtol(b, &endb, 10);

	if (*a == '\0' || *enda != '\0' || *b == '\0' || *endb != '\0')
		return 0;

	if (!strcmp(op, "-eq"))
		return x == y;
	if (!strcmp(op, "-ne"))
		return x != y;
	if (!strcmp(op, "-lt"))
		return x < y;
	if (!strcmp(op, "-le"))
		return x <= y;
	if (!strcmp(op, "-gt"))
		return x > y;
	if (!strcmp(op, "-ge"))
		return x >= y;
	return 0;
}

static int eval(int argc, char **argv)
{
	if (argc == 0)
		return 0;

	if (!strcmp(argv[0], "!") && argc > 1)
		return !eval(argc - 1, argv + 1);

	if (argc == 1)
		return argv[0][0] != '\0';

	if (argc == 2)
		return eval_unary(argv[0], argv[1]);

	if (argc == 3)
		return eval_binary(argv[0], argv[1], argv[2]);

	brk_cmd_usage(usage);
	return 2;
}

int main(int argc, char *argv[])
{
	bool bracket = brk_cmd_basename(argv[0])[0] == '[';

	brk_cmd_init(argc, argv);

	if (bracket) {
		if (argc < 2 || strcmp(argv[argc - 1], "]"))
			brk_cmd_usage(usage);
		argc--;
	}

	return eval(argc - 1, argv + 1) ? 0 : 1;
}
