#ifndef BRK_CMD_H
#define BRK_CMD_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

/*
 * Common exit statuses for user commands (GNU-style where applicable).
 */
#define BRK_CMD_EXIT_OK 0
#define BRK_CMD_EXIT_FAIL 1
#define BRK_CMD_EXIT_USAGE 2

/*
 * Well-known option ids; tools may use these or any positive integer / char.
 */
#define BRK_CMD_OPT_HELP 1

void brk_cmd_init(int argc, char *argv[]);
const char *brk_cmd_progname(void);
const char *brk_cmd_basename(const char *path);

struct passwd;

int brk_cmd_env_login(const struct passwd *pw, const char *term);
int brk_cmd_env_user(const struct passwd *pw);

const char *brk_cmd_term(void);
int brk_cmd_term_check(void);

void brk_cmd_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void brk_cmd_error_errno(const char *msg);
void brk_cmd_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

int brk_cmd_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int brk_cmd_fail_errno(const char *msg);

void brk_cmd_die(const char *fmt, ...) __attribute__((format(printf, 1, 2)))
__attribute__((noreturn));
void brk_cmd_die_errno(const char *msg) __attribute__((noreturn));

void brk_cmd_usage(const char *usage) __attribute__((noreturn));
void brk_cmd_usagef(const char *fmt, ...) __attribute__((format(printf, 1, 2)))
__attribute__((noreturn));

void brk_cmd_help_print(FILE *fp, const char *help);
void brk_cmd_help_exit(const char *help) __attribute__((noreturn));

enum brk_cmd_opt_arg {
	BRK_CMD_OPT_NO_ARG = 0,
	BRK_CMD_OPT_REQUIRED_ARG,
};

struct brk_cmd_option {
	int id;
	char short_opt;
	const char *long_opt;
	enum brk_cmd_opt_arg arg;
};

struct brk_cmd_optctx {
	int argc;
	char **argv;
	int index;
	int short_pos;
	const char *arg;
	bool end_of_opts;
};

#define BRK_CMD_OPTION_END { 0, 0, NULL, BRK_CMD_OPT_NO_ARG }

void brk_cmd_optctx_init(struct brk_cmd_optctx *ctx, int argc, char *argv[]);

/*
 * Parse the next option from argv.
 *
 * Return value:
 *   >0  matched option id
 *   0   no more options; operands begin at ctx->index
 *   '?' unknown option (message already printed)
 *   ':' missing required argument (message already printed)
 */
int brk_cmd_optparse(struct brk_cmd_optctx *ctx,
		     const struct brk_cmd_option *opts);

int brk_cmd_operand_count(const struct brk_cmd_optctx *ctx);
const char *brk_cmd_operand(const struct brk_cmd_optctx *ctx, int n);
void brk_cmd_require_operands(const struct brk_cmd_optctx *ctx, int min,
			      const char *usage);
void brk_cmd_require_operand_count(const struct brk_cmd_optctx *ctx, int exact,
				   const char *usage);
void brk_cmd_require_operand_range(const struct brk_cmd_optctx *ctx, int min,
				   int max, const char *usage);

int brk_cmd_parse_long(const char *s, long *out, long min, long max);
int brk_cmd_parse_ulong(const char *s, unsigned long *out, unsigned long min,
			unsigned long max);
int brk_cmd_parse_pid(const char *s, pid_t *out);

#endif
