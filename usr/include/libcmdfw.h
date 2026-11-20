#ifndef _LIBCMDFW_H
#define _LIBCMDFW_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

/*
 * Common exit statuses for user commands (GNU-style where applicable).
 */
#define CMDFW_EXIT_OK 0
#define CMDFW_EXIT_FAIL 1
#define CMDFW_EXIT_USAGE 2

/*
 * Well-known option ids; tools may use these or any positive integer / char.
 */
#define CMDFW_OPT_HELP 1

void cmdfw_init(int argc, char *argv[]);
const char *cmdfw_progname(void);
const char *cmdfw_basename(const char *path);

struct passwd;

int cmdfw_env_login(const struct passwd *pw, const char *term);
int cmdfw_env_user(const struct passwd *pw);

const char *cmdfw_term(void);
int cmdfw_term_check(void);

void cmdfw_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void cmdfw_error_errno(const char *msg);
void cmdfw_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

int cmdfw_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int cmdfw_fail_errno(const char *msg);

void cmdfw_die(const char *fmt, ...) __attribute__((format(printf, 1, 2)))
__attribute__((noreturn));
void cmdfw_die_errno(const char *msg) __attribute__((noreturn));

void cmdfw_usage(const char *usage) __attribute__((noreturn));
void cmdfw_usagef(const char *fmt, ...) __attribute__((format(printf, 1, 2)))
__attribute__((noreturn));

void cmdfw_help_print(FILE *fp, const char *help);
void cmdfw_help_exit(const char *help) __attribute__((noreturn));

enum cmdfw_opt_arg {
	CMDFW_OPT_NO_ARG = 0,
	CMDFW_OPT_REQUIRED_ARG,
};

struct cmdfw_option {
	int id;
	char short_opt;
	const char *long_opt;
	enum cmdfw_opt_arg arg;
};

struct cmdfw_optctx {
	int argc;
	char **argv;
	int index;
	int short_pos;
	const char *arg;
	bool end_of_opts;
};

#define CMDFW_OPTION_END { 0, 0, NULL, CMDFW_OPT_NO_ARG }

void cmdfw_optctx_init(struct cmdfw_optctx *ctx, int argc, char *argv[]);

/*
 * Parse the next option from argv.
 *
 * Return value:
 *   >0  matched option id
 *   0   no more options; operands begin at ctx->index
 *   '?' unknown option (message already printed)
 *   ':' missing required argument (message already printed)
 */
int cmdfw_optparse(struct cmdfw_optctx *ctx, const struct cmdfw_option *opts);

int cmdfw_operand_count(const struct cmdfw_optctx *ctx);
const char *cmdfw_operand(const struct cmdfw_optctx *ctx, int n);
void cmdfw_require_operands(const struct cmdfw_optctx *ctx, int min,
			    const char *usage);
void cmdfw_require_operand_count(const struct cmdfw_optctx *ctx, int exact,
				 const char *usage);
void cmdfw_require_operand_range(const struct cmdfw_optctx *ctx, int min,
				 int max, const char *usage);

int cmdfw_parse_long(const char *s, long *out, long min, long max);
int cmdfw_parse_ulong(const char *s, unsigned long *out, unsigned long min,
		      unsigned long max);
int cmdfw_parse_pid(const char *s, pid_t *out);

#endif
