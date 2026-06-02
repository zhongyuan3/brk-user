#include <apputil.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static void verror(const char *fmt, va_list ap)
{
	fprintf(stderr, "%s: ", app_progname());
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
}

void app_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror(fmt, ap);
	va_end(ap);
}

void app_error_errno(const char *msg)
{
	app_error("%s: %s", msg, strerror(errno));
}

void app_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: warning: ", app_progname());
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

int app_fail(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror(fmt, ap);
	va_end(ap);
	return APP_EXIT_FAIL;
}

int app_fail_errno(const char *msg)
{
	app_error_errno(msg);
	return APP_EXIT_FAIL;
}

void app_die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	verror(fmt, ap);
	va_end(ap);
	exit(APP_EXIT_FAIL);
}

void app_die_errno(const char *msg)
{
	app_error_errno(msg);
	exit(APP_EXIT_FAIL);
}

void app_usage(const char *usage)
{
	if (usage && usage[0])
		fprintf(stderr, "%s\n", usage);
	exit(APP_EXIT_USAGE);
}

void app_usagef(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "%s: ", app_progname());
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
	exit(APP_EXIT_USAGE);
}

void app_help_print(FILE *fp, const char *help)
{
	if (help)
		fputs(help, fp);
}

void app_help_exit(const char *help)
{
	app_help_print(stdout, help);
	exit(APP_EXIT_OK);
}
