#ifndef _APPUTIL_H
#define _APPUTIL_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

/*
 * Common exit statuses for user commands (GNU-style where applicable).
 */
#define APP_EXIT_OK 0
#define APP_EXIT_FAIL 1
#define APP_EXIT_USAGE 2

/*
 * Well-known option ids; tools may use these or any positive integer / char.
 */
#define APP_OPT_HELP 1

void app_init(int argc, char *argv[]);
const char *app_progname(void);
const char *app_basename(const char *path);

void app_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void app_error_errno(const char *msg);
void app_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

int app_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int app_fail_errno(const char *msg);

void app_die(const char *fmt, ...) __attribute__((format(printf, 1, 2)))
__attribute__((noreturn));
void app_die_errno(const char *msg) __attribute__((noreturn));

void app_usage(const char *usage) __attribute__((noreturn));
void app_usagef(const char *fmt, ...) __attribute__((format(printf, 1, 2)))
__attribute__((noreturn));

void app_help_print(FILE *fp, const char *help);
void app_help_exit(const char *help) __attribute__((noreturn));

enum app_opt_arg {
	APP_OPT_NO_ARG = 0,
	APP_OPT_REQUIRED_ARG,
};

struct app_option {
	int id;
	char short_opt;
	const char *long_opt;
	enum app_opt_arg arg;
};

struct app_optctx {
	int argc;
	char **argv;
	int index;
	int short_pos;
	const char *arg;
	bool end_of_opts;
};

#define APP_OPTION_END { 0, 0, NULL, APP_OPT_NO_ARG }

void app_optctx_init(struct app_optctx *ctx, int argc, char *argv[]);

/*
 * Parse the next option from argv.
 *
 * Return value:
 *   >0  matched option id
 *   0   no more options; operands begin at ctx->index
 *   '?' unknown option (message already printed)
 *   ':' missing required argument (message already printed)
 */
int app_optparse(struct app_optctx *ctx, const struct app_option *opts);

int app_operand_count(const struct app_optctx *ctx);
const char *app_operand(const struct app_optctx *ctx, int n);
void app_require_operands(const struct app_optctx *ctx, int min,
			  const char *usage);
void app_require_operand_count(const struct app_optctx *ctx, int exact,
			       const char *usage);
void app_require_operand_range(const struct app_optctx *ctx, int min, int max,
			       const char *usage);

int app_parse_long(const char *s, long *out, long min, long max);
int app_parse_ulong(const char *s, unsigned long *out, unsigned long min,
		    unsigned long max);
int app_parse_pid(const char *s, pid_t *out);

#endif
