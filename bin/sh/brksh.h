#ifndef SH_H
#define SH_H

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

typedef enum sh_err {
	SH_OK = 0,
	SH_ERR_NOMEM,
	SH_ERR_LEX,
	SH_ERR_PARSE,
	SH_ERR_IO,
	SH_ERR_SYS,
} sh_err_t;

const char *sh_err_default_msg(sh_err_t err);
void sh_err_fprint(FILE *fp, sh_err_t err, const char *detail);
int sh_err_to_exit_status(sh_err_t err);

enum sh_token_type {
	SH_TOKEN_EOF = 0,
	SH_TOKEN_WORD,
	SH_TOKEN_PIPE,
	SH_TOKEN_REDIR_IN,
	SH_TOKEN_REDIR_OUT,
	SH_TOKEN_REDIR_APPEND,
	SH_TOKEN_NEWLINE,
};

struct sh_token {
	enum sh_token_type type;
	char *text; /* owned when type == TOKEN_WORD */
};

struct sh_lexer {
	const char *input;
	size_t pos;
	size_t len;
	int line;
	int col;
	char errmsg[256];
};

void sh_lexer_init(struct sh_lexer *lex, const char *input, size_t len);
void sh_token_release(struct sh_token *tok);
sh_err_t sh_lexer_next(struct sh_lexer *lex, struct sh_token *tok);
char *sh_buf_push(char *buf, size_t *len, size_t *cap, char c);

enum sh_redir_kind {
	SH_REDIR_IN,
	SH_REDIR_OUT,
	SH_REDIR_APPEND,
};

struct sh_redir {
	enum sh_redir_kind kind;
	char *path;
	struct sh_redir *next;
};

struct sh_simple_cmd {
	char **argv; /* NULL-terminated */
	size_t argc;
	char **assign; /* NULL-terminated NAME=VALUE list */
	size_t nr_assign;
	struct sh_redir *redir_list;
};

struct sh_pipeline {
	struct sh_simple_cmd *cmds;
	size_t nr_cmds;
};

struct sh_parse_result {
	sh_err_t err;
	const char *errmsg;
};

void sh_pipeline_init(struct sh_pipeline *p);
void sh_pipeline_release(struct sh_pipeline *p);
sh_err_t sh_parse_line(struct sh_lexer *lex, struct sh_pipeline *out,
		       struct sh_parse_result *res);
sh_err_t sh_pipeline_exec(const struct sh_pipeline *p, int *exit_status);

extern char cwd[1024];
extern int sh_last_status;
extern int sh_argc;
extern char **sh_argv;
extern const char *sh_arg0;

void sh_builtin_exit(struct sh_pipeline *p) __attribute__((noreturn));
sh_err_t sh_builtin_cd(struct sh_pipeline *p);
void sh_builtin_pwd(void);
int sh_builtin_alias(struct sh_pipeline *p);
int sh_builtin_export(struct sh_pipeline *p);
int sh_builtin_unset(struct sh_pipeline *p);
int sh_alias_expand_cmd(struct sh_simple_cmd *cmd);

int sh_valid_env_name(const char *name);
int sh_cmd_extract_assignments(struct sh_simple_cmd *cmd);
int sh_apply_assignments(const struct sh_simple_cmd *cmd);

struct sh_env_save {
	char *name;
	char *value; /* NULL when the variable was unset */
};

int sh_env_save_assignments(const struct sh_simple_cmd *cmd,
			    struct sh_env_save **out, size_t *nr);
void sh_env_restore(struct sh_env_save *saves, size_t nr);

enum sh_edit_status {
	SH_EDIT_OK = 0,
	SH_EDIT_EOF,
	SH_EDIT_CANCEL,
	SH_EDIT_ERR,
};

int sh_tty_raw(void);
void sh_tty_restore(void);
void sh_tty_fatal(int sig);

void sh_history_add(const char *line);
int sh_edit_line(const char *prompt, size_t prompt_len, char **out);

#endif
