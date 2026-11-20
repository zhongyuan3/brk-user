#include <brk/cmd.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void verror(const char *fmt, va_list ap)
{
	fprintf(stderr, "%s: ", brk_cmd_progname());
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
}

void brk_cmd_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror(fmt, ap);
	va_end(ap);
}

void brk_cmd_error_errno(const char *msg)
{
	brk_cmd_error("%s: %s", msg, strerror(errno));
}

void brk_cmd_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: warning: ", brk_cmd_progname());
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

int brk_cmd_fail(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror(fmt, ap);
	va_end(ap);
	return BRK_CMD_EXIT_FAIL;
}

int brk_cmd_fail_errno(const char *msg)
{
	brk_cmd_error_errno(msg);
	return BRK_CMD_EXIT_FAIL;
}

void brk_cmd_die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror(fmt, ap);
	va_end(ap);
	exit(BRK_CMD_EXIT_FAIL);
}

void brk_cmd_die_errno(const char *msg)
{
	brk_cmd_error_errno(msg);
	exit(BRK_CMD_EXIT_FAIL);
}

void brk_cmd_usage(const char *usage)
{
	if (usage && usage[0])
		fprintf(stderr, "%s\n", usage);
	exit(BRK_CMD_EXIT_USAGE);
}

void brk_cmd_usagef(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", brk_cmd_progname());
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(BRK_CMD_EXIT_USAGE);
}

void brk_cmd_help_print(FILE *fp, const char *help)
{
	if (help)
		fputs(help, fp);
}

void brk_cmd_help_exit(const char *help)
{
	brk_cmd_help_print(stdout, help);
	if (help && help[0] && help[strlen(help) - 1] != '\n')
		fputc('\n', stdout);
	exit(BRK_CMD_EXIT_OK);
}
