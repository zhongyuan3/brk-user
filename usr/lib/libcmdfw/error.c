#include <libcmdfw.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void verror(const char *fmt, va_list ap)
{
	fprintf(stderr, "%s: ", cmdfw_progname());
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
}

void cmdfw_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror(fmt, ap);
	va_end(ap);
}

void cmdfw_error_errno(const char *msg)
{
	cmdfw_error("%s: %s", msg, strerror(errno));
}

void cmdfw_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: warning: ", cmdfw_progname());
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

int cmdfw_fail(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror(fmt, ap);
	va_end(ap);
	return CMDFW_EXIT_FAIL;
}

int cmdfw_fail_errno(const char *msg)
{
	cmdfw_error_errno(msg);
	return CMDFW_EXIT_FAIL;
}

void cmdfw_die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror(fmt, ap);
	va_end(ap);
	exit(CMDFW_EXIT_FAIL);
}

void cmdfw_die_errno(const char *msg)
{
	cmdfw_error_errno(msg);
	exit(CMDFW_EXIT_FAIL);
}

void cmdfw_usage(const char *usage)
{
	if (usage && usage[0])
		fprintf(stderr, "%s\n", usage);
	exit(CMDFW_EXIT_USAGE);
}

void cmdfw_usagef(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", cmdfw_progname());
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(CMDFW_EXIT_USAGE);
}

void cmdfw_help_print(FILE *fp, const char *help)
{
	if (help)
		fputs(help, fp);
}

void cmdfw_help_exit(const char *help)
{
	cmdfw_help_print(stdout, help);
	if (help && help[0] && help[strlen(help) - 1] != '\n')
		fputc('\n', stdout);
	exit(CMDFW_EXIT_OK);
}
