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

static const char *sh_err_default_msg(sh_err_t err)
{
	switch (err) {
	case SH_OK:
		return "success";
	case SH_ERR_NOMEM:
		return "out of memory";
	case SH_ERR_LEX:
		return "lexical error";
	case SH_ERR_PARSE:
		return "parse error";
	case SH_ERR_IO:
		return "input/output error";
	case SH_ERR_SYS:
		return "system call failed";
	}
	return "unknown error";
}

static void sh_err_fprint(FILE *fp, sh_err_t err, const char *detail)
{
	if (detail && detail[0])
		fprintf(fp, "sh: %s\n", detail);
	else
		fprintf(fp, "sh: %s\n", sh_err_default_msg(err));
}

static int sh_err_to_exit_status(sh_err_t err)
{
	switch (err) {
	case SH_OK:
		return 0;
	case SH_ERR_LEX:
	case SH_ERR_PARSE:
		return 2;
	case SH_ERR_NOMEM:
	case SH_ERR_IO:
	case SH_ERR_SYS:
	default:
		return 1;
	}
}

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

static void sh_lexer_init(struct sh_lexer *lex, const char *input, size_t len)
{
	lex->input = input;
	lex->pos = 0;
	lex->len = len;
	lex->line = 1;
	lex->col = 1;
	lex->errmsg[0] = '\0';
}

static void sh_token_release(struct sh_token *tok)
{
	if (tok && tok->type == SH_TOKEN_WORD && tok->text) {
		free(tok->text);
		tok->text = NULL;
	}
}

static int at_eof(const struct sh_lexer *lex)
{
	return lex->pos >= lex->len;
}

static char peek(const struct sh_lexer *lex)
{
	return at_eof(lex) ? '\0' : lex->input[lex->pos];
}

static void advance(struct sh_lexer *lex)
{
	if (at_eof(lex))
		return;
	if (lex->input[lex->pos] == '\n') {
		lex->line++;
		lex->col = 1;
	} else {
		lex->col++;
	}
	lex->pos++;
}

static void skip_spaces(struct sh_lexer *lex)
{
	while (!at_eof(lex) && isspace((unsigned char)peek(lex)) &&
	       peek(lex) != '\n')
		advance(lex);
}

static void skip_comment(struct sh_lexer *lex)
{
	if (peek(lex) != '#')
		return;
	while (!at_eof(lex) && peek(lex) != '\n')
		advance(lex);
}

static char *buf_push(char *buf, size_t *len, size_t *cap, char c)
{
	if (*len + 1 >= *cap) {
		size_t ncap = *cap ? *cap * 2 : 64;
		char *nb = realloc(buf, ncap);

		if (!nb)
			return NULL;
		buf = nb;
		*cap = ncap;
	}
	buf[(*len)++] = c;
	return buf;
}

/*
 * Expand $NAME and ${NAME} from the process environment.  Returns the
 * updated buffer and sets *consumed when a variable reference was consumed.
 */
static char *expand_variable(struct sh_lexer *lex, char *buf, size_t *len,
			     size_t *cap, int *consumed)
{
	char name[128];
	size_t name_len = 0;
	size_t save = lex->pos;
	const char *value;
	int braced = 0;

	*consumed = 0;
	advance(lex); /* $ */

	if (peek(lex) == '{') {
		braced = 1;
		advance(lex);
	}

	if (!braced &&
	    !(isalpha((unsigned char)peek(lex)) || peek(lex) == '_')) {
		lex->pos = save;
		return buf;
	}

	while (!at_eof(lex) &&
	       (isalnum((unsigned char)peek(lex)) || peek(lex) == '_')) {
		if (name_len + 1 < sizeof(name))
			name[name_len] = peek(lex);
		name_len++;
		advance(lex);
	}

	if (braced) {
		if (peek(lex) != '}' || name_len == 0) {
			lex->pos = save;
			return buf;
		}
		advance(lex);
	}

	if (name_len == 0 || name_len >= sizeof(name)) {
		lex->pos = save;
		return buf;
	}
	name[name_len] = '\0';

	value = getenv(name);
	if (value) {
		for (const char *p = value; *p; p++) {
			buf = buf_push(buf, len, cap, *p);
			if (!buf)
				return NULL;
		}
	}

	*consumed = 1;
	return buf;
}

static int read_single_quoted(struct sh_lexer *lex, char **out)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;

	advance(lex); /* opening ' */
	while (!at_eof(lex) && peek(lex) != '\'') {
		buf = buf_push(buf, &len, &cap, peek(lex));
		if (!buf)
			return -1;
		advance(lex);
	}
	if (peek(lex) != '\'') {
		free(buf);
		return -2; /* unterminated */
	}
	advance(lex); /* closing ' */
	buf = buf_push(buf, &len, &cap, '\0');
	if (!buf)
		return -1;
	*out = buf;
	return 0;
}

static int read_double_quoted(struct sh_lexer *lex, char **out)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;

	advance(lex); /* opening " */
	while (!at_eof(lex) && peek(lex) != '"') {
		if (peek(lex) == '\\') {
			char n;

			advance(lex);
			if (at_eof(lex)) {
				free(buf);
				return -2;
			}
			n = peek(lex);
			if (n == '\n') {
				free(buf);
				return -2;
			}
			if (n == '"' || n == '\\' || n == '$' || n == '`') {
				advance(lex);
				buf = buf_push(buf, &len, &cap, n);
			} else {
				buf = buf_push(buf, &len, &cap, '\\');
				buf = buf_push(buf, &len, &cap, n);
				advance(lex);
			}
		} else if (peek(lex) == '$') {
			int consumed;

			buf = expand_variable(lex, buf, &len, &cap, &consumed);
			if (!buf)
				return -1;
			if (!consumed) {
				buf = buf_push(buf, &len, &cap, '$');
				if (!buf)
					return -1;
				advance(lex);
			}
		} else {
			buf = buf_push(buf, &len, &cap, peek(lex));
			advance(lex);
		}
		if (!buf)
			return -1;
	}
	if (peek(lex) != '"') {
		free(buf);
		return -2;
	}
	advance(lex);
	buf = buf_push(buf, &len, &cap, '\0');
	if (!buf)
		return -1;
	*out = buf;
	return 0;
}

static int read_word(struct sh_lexer *lex, char **out)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;

	while (!at_eof(lex)) {
		char c = peek(lex);

		if (c == '\'') {
			char *inner;
			int r = read_single_quoted(lex, &inner);

			if (r)
				goto fail;
			for (size_t i = 0; inner[i]; i++) {
				buf = buf_push(buf, &len, &cap, inner[i]);
				if (!buf) {
					free(inner);
					return -1;
				}
			}
			free(inner);
			continue;
		}
		if (c == '"') {
			char *inner;
			int r = read_double_quoted(lex, &inner);

			if (r)
				goto fail;
			for (size_t i = 0; inner[i]; i++) {
				buf = buf_push(buf, &len, &cap, inner[i]);
				if (!buf) {
					free(inner);
					return -1;
				}
			}
			free(inner);
			continue;
		}
		if (isspace((unsigned char)c) || c == '|' || c == '<' ||
		    c == '>' || c == '#' || c == '\0')
			break;
		if (c == '$') {
			int consumed;

			buf = expand_variable(lex, buf, &len, &cap, &consumed);
			if (!buf)
				return -1;
			if (!consumed) {
				buf = buf_push(buf, &len, &cap, '$');
				if (!buf)
					return -1;
				advance(lex);
			}
			continue;
		}
		if (c == '\\' && lex->pos + 1 < lex->len) {
			advance(lex);
			c = peek(lex);
		}
		buf = buf_push(buf, &len, &cap, c);
		if (!buf)
			return -1;
		advance(lex);
	}
	buf = buf_push(buf, &len, &cap, '\0');
	if (!buf)
		return -1;
	*out = buf;
	return 0;
fail:
	free(buf);
	return -2;
}

static sh_err_t sh_lexer_next(struct sh_lexer *lex, struct sh_token *tok)
{
next:
	tok->type = SH_TOKEN_EOF;
	tok->text = NULL;

	skip_spaces(lex);
	if (at_eof(lex))
		return SH_OK;

	if (peek(lex) == '\n') {
		advance(lex);
		tok->type = SH_TOKEN_NEWLINE;
		return SH_OK;
	}

	if (peek(lex) == '#') {
		skip_comment(lex);
		goto next; /* avoid recursive call */
	}

	if (peek(lex) == '|') {
		advance(lex);
		tok->type = SH_TOKEN_PIPE;
		return SH_OK;
	}

	if (peek(lex) == '<') {
		advance(lex);
		tok->type = SH_TOKEN_REDIR_IN;
		return SH_OK;
	}

	if (peek(lex) == '>') {
		advance(lex);
		if (peek(lex) == '>') {
			advance(lex);
			tok->type = SH_TOKEN_REDIR_APPEND;
		} else {
			tok->type = SH_TOKEN_REDIR_OUT;
		}
		return SH_OK;
	}

	int rw = read_word(lex, &tok->text);
	if (rw != 0) {
		sh_err_t e;

		sh_token_release(tok);
		tok->type = SH_TOKEN_EOF;
		if (rw == -2) {
			snprintf(lex->errmsg, sizeof(lex->errmsg),
				 "unterminated quote");
			e = SH_ERR_LEX;
		} else {
			snprintf(lex->errmsg, sizeof(lex->errmsg),
				 "out of memory");
			e = SH_ERR_NOMEM;
		}
		return e;
	}
	tok->type = SH_TOKEN_WORD;
	return SH_OK;
}

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
	struct sh_redir *redir_list;
};

struct sh_pipeline {
	struct sh_simple_cmd *cmds;
	size_t nr_cmds;
};

static void sh_pipeline_init(struct sh_pipeline *p)
{
	p->cmds = NULL;
	p->nr_cmds = 0;
}

static void redir_list_release(struct sh_redir *r)
{
	while (r) {
		struct sh_redir *n = r->next;

		free(r->path);
		free(r);
		r = n;
	}
}

static void sh_pipeline_release(struct sh_pipeline *p)
{
	size_t i;

	if (!p)
		return;

	for (i = 0; i < p->nr_cmds; i++) {
		struct sh_simple_cmd *c = &p->cmds[i];

		if (c->argv) {
			size_t j;

			for (j = 0; c->argv[j]; j++)
				free(c->argv[j]);
			free(c->argv);
		}
		redir_list_release(c->redir_list);
		c->argv = NULL;
		c->argc = 0;
		c->redir_list = NULL;
	}
	free(p->cmds);
	p->cmds = NULL;
	p->nr_cmds = 0;
}

static int apply_redirs_child(const struct sh_simple_cmd *c)
{
	struct sh_redir *r;

	for (r = c->redir_list; r; r = r->next) {
		int fd;
		mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

		if (r->kind == SH_REDIR_IN) {
			fd = open(r->path, O_RDONLY);
		} else if (r->kind == SH_REDIR_OUT) {
			fd = open(r->path, O_WRONLY | O_CREAT | O_TRUNC, mode);
		} else {
			fd = open(r->path, O_WRONLY | O_CREAT | O_APPEND, mode);
		}
		if (fd < 0) {
			fprintf(stderr, "sh: %s: %s\n", r->path,
				strerror(errno));
			return -1;
		}

		int target = (r->kind == SH_REDIR_IN) ? STDIN_FILENO :
							STDOUT_FILENO;

		if (dup2(fd, target) < 0) {
			fprintf(stderr, "sh: dup2: %s\n", strerror(errno));
			close(fd);
			return -1;
		}
		close(fd);
	}
	return 0;
}

static void run_child(const struct sh_simple_cmd *c)
{
	/* The shell ignores these; jobs must receive the defaults again. */
	signal(SIGINT, SIG_DFL);
	signal(SIGQUIT, SIG_DFL);

	if (apply_redirs_child(c) < 0)
		_exit(127);
	execvp(c->argv[0], c->argv);
	fprintf(stderr, "sh: %s: %s\n", c->argv[0], strerror(errno));
	_exit(127);
}

static sh_err_t sh_pipeline_exec(const struct sh_pipeline *p, int *exit_status)
{
	size_t n = p->nr_cmds;
	int (*pipes)[2] = NULL;
	pid_t *kids;
	size_t i;

	if (exit_status)
		*exit_status = 0;

	if (n == 0)
		return SH_OK;

	if (n > 1) {
		pipes = calloc(n - 1, sizeof(*pipes));
		if (!pipes)
			return SH_ERR_NOMEM;
		for (i = 0; i < n - 1; i++) {
			if (pipe(pipes[i]) < 0) {
				perror("sh: pipe");
				free(pipes);
				return SH_ERR_SYS;
			}
		}
	}

	kids = calloc(n, sizeof(*kids));
	if (!kids) {
		if (pipes) {
			for (i = 0; i < n - 1; i++) {
				close(pipes[i][0]);
				close(pipes[i][1]);
			}
			free(pipes);
		}
		return SH_ERR_NOMEM;
	}

	for (i = 0; i < n; i++) {
		pid_t pid = fork();

		if (pid < 0) {
			size_t j;

			perror("sh: fork");
			for (j = 0; j < i; j++)
				waitpid(kids[j], NULL, 0);
			if (pipes) {
				size_t k;

				for (k = 0; k < n - 1; k++) {
					close(pipes[k][0]);
					close(pipes[k][1]);
				}
				free(pipes);
			}
			free(kids);
			return SH_ERR_SYS;
		}
		if (pid == 0) {
			if (pipes) {
				size_t j;

				if (i > 0) {
					if (dup2(pipes[i - 1][0],
						 STDIN_FILENO) < 0) {
						perror("sh: dup2");
						_exit(127);
					}
				}
				if (i < n - 1) {
					if (dup2(pipes[i][1], STDOUT_FILENO) <
					    0) {
						perror("sh: dup2");
						_exit(127);
					}
				}
				for (j = 0; j < n - 1; j++) {
					close(pipes[j][0]);
					close(pipes[j][1]);
				}
			}
			run_child(&p->cmds[i]);
		}
		kids[i] = pid;
	}

	if (pipes) {
		for (i = 0; i < n - 1; i++) {
			close(pipes[i][0]);
			close(pipes[i][1]);
		}
		free(pipes);
	}

	int last_status = 0;
	sh_err_t err = SH_OK;

	for (i = 0; i < n; i++) {
		int st = 0;

		if (waitpid(kids[i], &st, 0) < 0) {
			perror("sh: waitpid");
			err = SH_ERR_SYS;
			for (i++; i < n; i++)
				waitpid(kids[i], NULL, 0);
			break;
		}
		if (i == n - 1)
			last_status = st;
	}
	free(kids);

	if (err != SH_OK) {
		if (exit_status)
			*exit_status = 1;
		return err;
	}

	if (exit_status) {
		if (WIFEXITED(last_status))
			*exit_status = WEXITSTATUS(last_status);
		else if (WIFSIGNALED(last_status))
			*exit_status = 128 + WTERMSIG(last_status);
		else
			*exit_status = 1;
	}
	return SH_OK;
}

struct sh_parse_result {
	sh_err_t err;
	const char *errmsg;
};

static int argv_push(struct sh_simple_cmd *c, char *w)
{
	char **na = realloc(c->argv, (c->argc + 2) * sizeof(char *));

	if (!na)
		return -1;
	c->argv = na;
	c->argv[c->argc++] = w;
	c->argv[c->argc] = NULL;
	return 0;
}

static void parse_fail(struct sh_pipeline *out, struct sh_parse_result *res,
		       sh_err_t err, const char *msg)
{
	res->err = err;
	res->errmsg = msg;
	sh_pipeline_release(out);
	sh_pipeline_init(out);
}

static struct sh_simple_cmd *tail_cmd(struct sh_pipeline *p)
{
	if (p->nr_cmds == 0)
		return NULL;
	return &p->cmds[p->nr_cmds - 1];
}

static int cur_has_program(struct sh_pipeline *p)
{
	struct sh_simple_cmd *c = tail_cmd(p);

	return c && c->argc > 0;
}

static int push_cmd(struct sh_pipeline *p)
{
	struct sh_simple_cmd *n;

	n = realloc(p->cmds, (p->nr_cmds + 1) * sizeof(*n));
	if (!n)
		return -1;
	p->cmds = n;

	struct sh_simple_cmd *c = &p->cmds[p->nr_cmds++];

	c->argv = NULL;
	c->argc = 0;
	c->redir_list = NULL;

	return 0;
}

static int ensure_cmd(struct sh_pipeline *p)
{
	if (p->nr_cmds == 0)
		return push_cmd(p);
	return 0;
}

static int redir_append(struct sh_simple_cmd *c, enum sh_redir_kind k,
			char *path)
{
	struct sh_redir *r = malloc(sizeof(*r));

	if (!r)
		return -1;
	r->kind = k;
	r->path = path;
	r->next = NULL;
	if (!c->redir_list) {
		c->redir_list = r;
	} else {
		struct sh_redir *x = c->redir_list;

		while (x->next)
			x = x->next;
		x->next = r;
	}
	return 0;
}

static sh_err_t sh_parse_line(struct sh_lexer *lex, struct sh_pipeline *out,
			      struct sh_parse_result *res)
{
	sh_err_t le;

	res->err = SH_OK;
	res->errmsg = NULL;
	sh_pipeline_init(out);

	for (;;) {
		struct sh_token t;

		memset(&t, 0, sizeof(t));
		le = sh_lexer_next(lex, &t);
		if (le != SH_OK) {
			const char *msg = lex->errmsg[0] ?
						  (const char *)lex->errmsg :
						  sh_err_default_msg(le);

			parse_fail(out, res, le, msg);
			return le;
		}

		if (t.type == SH_TOKEN_NEWLINE || t.type == SH_TOKEN_EOF) {
			sh_token_release(&t);
			break;
		}

		if (t.type == SH_TOKEN_PIPE) {
			sh_token_release(&t);
			if (!cur_has_program(out)) {
				parse_fail(
					out, res, SH_ERR_PARSE,
					"syntax error near unexpected token `|'");
				return SH_ERR_PARSE;
			}
			if (push_cmd(out)) {
				parse_fail(out, res, SH_ERR_NOMEM,
					   sh_err_default_msg(SH_ERR_NOMEM));
				return SH_ERR_NOMEM;
			}
			continue;
		}

		if (t.type == SH_TOKEN_REDIR_IN ||
		    t.type == SH_TOKEN_REDIR_OUT ||
		    t.type == SH_TOKEN_REDIR_APPEND) {
			enum sh_token_type rk = t.type;

			sh_token_release(&t);
			if (ensure_cmd(out)) {
				parse_fail(out, res, SH_ERR_NOMEM,
					   sh_err_default_msg(SH_ERR_NOMEM));
				return SH_ERR_NOMEM;
			}

			struct sh_token path;

			memset(&path, 0, sizeof(path));
			le = sh_lexer_next(lex, &path);
			if (le != SH_OK) {
				const char *msg =
					lex->errmsg[0] ?
						(const char *)lex->errmsg :
						sh_err_default_msg(le);

				parse_fail(out, res, le, msg);
				return le;
			}
			if (path.type != SH_TOKEN_WORD) {
				sh_token_release(&path);
				parse_fail(
					out, res, SH_ERR_PARSE,
					"syntax error: redirection needs a filename");
				return SH_ERR_PARSE;
			}

			enum sh_redir_kind k = SH_REDIR_IN;

			if (rk == SH_TOKEN_REDIR_OUT)
				k = SH_REDIR_OUT;
			else if (rk == SH_TOKEN_REDIR_APPEND)
				k = SH_REDIR_APPEND;
			if (redir_append(tail_cmd(out), k, path.text)) {
				sh_token_release(&path);
				parse_fail(out, res, SH_ERR_NOMEM,
					   sh_err_default_msg(SH_ERR_NOMEM));
				return SH_ERR_NOMEM;
			}
			path.text = NULL;

			sh_token_release(&path);

			continue;
		}

		if (t.type == SH_TOKEN_WORD) {
			if (ensure_cmd(out)) {
				sh_token_release(&t);
				parse_fail(out, res, SH_ERR_NOMEM,
					   sh_err_default_msg(SH_ERR_NOMEM));
				return SH_ERR_NOMEM;
			}
			if (argv_push(tail_cmd(out), t.text)) {
				sh_token_release(&t);
				parse_fail(out, res, SH_ERR_NOMEM,
					   sh_err_default_msg(SH_ERR_NOMEM));
				return SH_ERR_NOMEM;
			}
			t.text = NULL;
			sh_token_release(&t);
			continue;
		}

		sh_token_release(&t);
		parse_fail(out, res, SH_ERR_PARSE,
			   "syntax error: unexpected token");
		return SH_ERR_PARSE;
	}

	if (out->nr_cmds == 0)
		return SH_OK;

	struct sh_simple_cmd *last = tail_cmd(out);

	if (last->argc == 0) {
		parse_fail(out, res, SH_ERR_PARSE,
			   "syntax error: missing command");
		return SH_ERR_PARSE;
	}

	return SH_OK;
}

static char cwd[1024];
static void builtin_exit(struct sh_pipeline *p) __attribute__((noreturn));

static void chomp_crlf(char *s)
{
	size_t n = strlen(s);

	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
		s[--n] = '\0';
}

static void builtin_exit(struct sh_pipeline *p)
{
	struct sh_simple_cmd *c = &p->cmds[0];
	int code = 0;

	if (c->argv[1]) {
		char *end = NULL;
		long v = strtol(c->argv[1], &end, 10);

		if (end == c->argv[1] || *end != '\0' || v < 0 || v > 255) {
			fprintf(stderr,
				"sh: exit: numeric argument required\n");
			code = 2;
		} else {
			code = (int)v;
		}
	}
	sh_pipeline_release(p);
	exit(code);
}

static sh_err_t builtin_cd(struct sh_pipeline *p)
{
	struct sh_simple_cmd *c = &p->cmds[0];

	if (c->argc > 1) {
		if (chdir(c->argv[1]) < 0) {
			perror("sh: cd");
			return SH_ERR_IO;
		}
		if (!getcwd(cwd, sizeof(cwd) - 1)) {
			perror("sh: getcwd");
			return SH_ERR_IO;
		}
		cwd[sizeof(cwd) - 1] = '\0';
	}
	return SH_OK;
}

static void builtin_pwd(void)
{
	puts(cwd);
}

struct sh_alias {
	char *name;
	char *value;
	struct sh_alias *next;
};

static struct sh_alias *sh_aliases;

static void sh_alias_free_words(char **words, size_t count)
{
	for (size_t i = 0; i < count; i++)
		free(words[i]);
	free(words);
}

static int sh_alias_split(const char *value, char ***out, size_t *out_count)
{
	struct sh_lexer lex;
	struct sh_token token;
	char **words = NULL;
	size_t count = 0;
	size_t cap = 0;

	sh_lexer_init(&lex, value, strlen(value));
	for (;;) {
		sh_err_t err = sh_lexer_next(&lex, &token);

		if (err != SH_OK) {
			sh_alias_free_words(words, count);
			return -1;
		}
		if (token.type == SH_TOKEN_EOF)
			break;
		if (token.type != SH_TOKEN_WORD) {
			sh_token_release(&token);
			sh_alias_free_words(words, count);
			return -1;
		}

		if (count == cap) {
			size_t ncap = cap ? cap * 2 : 8;
			char **nwords = realloc(words, ncap * sizeof(*nwords));

			if (!nwords) {
				sh_token_release(&token);
				sh_alias_free_words(words, count);
				return -1;
			}
			words = nwords;
			cap = ncap;
		}
		words[count++] = token.text;
	}

	if (count == 0) {
		free(words);
		return -1;
	}
	*out = words;
	*out_count = count;
	return 0;
}

static struct sh_alias *sh_alias_find(const char *name)
{
	struct sh_alias *alias;

	for (alias = sh_aliases; alias; alias = alias->next)
		if (!strcmp(alias->name, name))
			return alias;
	return NULL;
}

static int sh_valid_alias_name(const char *name)
{
	if (!name[0])
		return 0;
	for (; *name; name++) {
		if (isspace((unsigned char)*name) ||
		    strchr("=|<>\'\"\\#", *name))
			return 0;
	}
	return 1;
}

static int sh_valid_env_name(const char *name)
{
	if (!isalpha((unsigned char)name[0]) && name[0] != '_')
		return 0;
	for (name++; *name; name++)
		if (!isalnum((unsigned char)*name) && *name != '_')
			return 0;
	return 1;
}

static int sh_alias_set(const char *name, const char *value)
{
	char **words;
	size_t count;
	struct sh_alias *alias;
	char *copy;

	if (!sh_valid_alias_name(name))
		return -1;
	if (sh_alias_split(value, &words, &count) != 0)
		return -1;
	sh_alias_free_words(words, count);

	copy = strdup(value);
	if (!copy)
		return -1;

	alias = sh_alias_find(name);
	if (alias) {
		free(alias->value);
		alias->value = copy;
		return 0;
	}

	alias = malloc(sizeof(*alias));
	if (!alias) {
		free(copy);
		return -1;
	}
	alias->name = strdup(name);
	if (!alias->name) {
		free(copy);
		free(alias);
		return -1;
	}
	alias->value = copy;
	alias->next = sh_aliases;
	sh_aliases = alias;
	return 0;
}

static void sh_alias_print(const struct sh_alias *alias)
{
	printf("alias %s='", alias->name);
	for (const char *p = alias->value; *p; p++) {
		if (*p == '\'')
			fputs("'\\''", stdout);
		else
			putchar(*p);
	}
	puts("'");
}

static int sh_alias_expand_cmd(struct sh_simple_cmd *cmd)
{
	int depth = 0;

	while (cmd->argc > 0 && cmd->argv[0]) {
		struct sh_alias *alias = sh_alias_find(cmd->argv[0]);
		char **words;
		size_t count;
		size_t old_argc;
		char **old_argv;
		char **new_argv;
		int stop;

		if (!alias)
			return 0;
		if (++depth > 16) {
			fprintf(stderr, "sh: alias: expansion too deep\n");
			return -1;
		}
		if (sh_alias_split(alias->value, &words, &count) != 0)
			return -1;

		stop = !strcmp(words[0], alias->name);
		old_argc = cmd->argc;
		old_argv = cmd->argv;
		new_argv = malloc((count + old_argc) * sizeof(*new_argv));
		if (!new_argv) {
			sh_alias_free_words(words, count);
			return -1;
		}

		for (size_t i = 0; i < count; i++)
			new_argv[i] = words[i];
		for (size_t i = 1; i < old_argc; i++)
			new_argv[count + i - 1] = old_argv[i];
		new_argv[count + old_argc - 1] = NULL;

		free(words);
		free(old_argv[0]);
		free(old_argv);
		cmd->argv = new_argv;
		cmd->argc = count + old_argc - 1;

		if (stop)
			return 0;
	}
	return 0;
}

static int builtin_alias(struct sh_pipeline *p)
{
	struct sh_simple_cmd *cmd = &p->cmds[0];
	int status = 0;

	if (cmd->argc == 1) {
		for (struct sh_alias *alias = sh_aliases; alias;
		     alias = alias->next)
			sh_alias_print(alias);
		return 0;
	}

	for (size_t i = 1; i < cmd->argc; i++) {
		char *arg = cmd->argv[i];
		char *eq = strchr(arg, '=');

		if (eq) {
			*eq = '\0';
			if (sh_alias_set(arg, eq + 1) != 0) {
				fprintf(stderr,
					"sh: alias: invalid alias '%s'\n", arg);
				status = 1;
			}
			*eq = '=';
		} else {
			struct sh_alias *alias = sh_alias_find(arg);

			if (!alias) {
				fprintf(stderr, "sh: alias: %s: not found\n",
					arg);
				status = 1;
			} else {
				sh_alias_print(alias);
			}
		}
	}
	return status;
}

static int builtin_export(struct sh_pipeline *p)
{
	struct sh_simple_cmd *cmd = &p->cmds[0];
	int status = 0;

	if (cmd->argc == 1) {
		for (char **entry = environ; entry && *entry; entry++)
			puts(*entry);
		return 0;
	}

	for (size_t i = 1; i < cmd->argc; i++) {
		char *arg = cmd->argv[i];
		char *eq = strchr(arg, '=');

		if (eq) {
			*eq = '\0';
			if (!sh_valid_env_name(arg)) {
				fprintf(stderr,
					"sh: export: invalid name '%s'\n", arg);
				status = 1;
			} else if (setenv(arg, eq + 1, 1) != 0) {
				perror("sh: export");
				status = 1;
			}
			*eq = '=';
		} else {
			if (!sh_valid_env_name(arg)) {
				fprintf(stderr,
					"sh: export: invalid name '%s'\n", arg);
				status = 1;
			} else if (!getenv(arg) && setenv(arg, "", 1) != 0) {
				perror("sh: export");
				status = 1;
			}
		}
	}
	return status;
}

static int run_line(const char *line, size_t len)
{
	struct sh_lexer lex;
	struct sh_pipeline pl;
	struct sh_parse_result res;
	sh_err_t err;
	int cmd_exit = 0;

	sh_lexer_init(&lex, line, len);
	err = sh_parse_line(&lex, &pl, &res);
	if (err != SH_OK) {
		sh_err_fprint(stderr, err, res.errmsg);
		return sh_err_to_exit_status(err);
	}
	if (pl.nr_cmds == 0) {
		sh_pipeline_release(&pl);
		return 0;
	}

	for (size_t i = 0; i < pl.nr_cmds; i++) {
		if (sh_alias_expand_cmd(&pl.cmds[i]) != 0) {
			fprintf(stderr, "sh: alias expansion failed\n");
			sh_pipeline_release(&pl);
			return 1;
		}
	}

	if (pl.nr_cmds == 1 && pl.cmds[0].argv[0]) {
		const char *a0 = pl.cmds[0].argv[0];

		if (!strcmp(a0, "exit"))
			builtin_exit(&pl);
		if (!strcmp(a0, "cd")) {
			err = builtin_cd(&pl);
			sh_pipeline_release(&pl);
			if (err != SH_OK)
				return sh_err_to_exit_status(err);
			return 0;
		}
		if (!strcmp(a0, "pwd")) {
			builtin_pwd();
			sh_pipeline_release(&pl);
			return 0;
		}
		if (!strcmp(a0, "alias")) {
			int status = builtin_alias(&pl);

			sh_pipeline_release(&pl);
			return status;
		}
		if (!strcmp(a0, "export")) {
			int status = builtin_export(&pl);

			sh_pipeline_release(&pl);
			return status;
		}
	}

	err = sh_pipeline_exec(&pl, &cmd_exit);
	sh_pipeline_release(&pl);
	if (err != SH_OK) {
		if (err != SH_ERR_SYS)
			sh_err_fprint(stderr, err, NULL);
		return sh_err_to_exit_status(err);
	}
	return cmd_exit;
}

static int run_script(const char *path)
{
	FILE *f = fopen(path, "r");
	char *line = NULL;
	size_t cap = 0;
	ssize_t nread;
	int last = 0;

	if (!f) {
		perror(path);
		return sh_err_to_exit_status(SH_ERR_IO);
	}
	while ((nread = getline(&line, &cap, f)) != -1) {
		chomp_crlf(line);
		last = run_line(line, strlen(line));
	}
	if (ferror(f)) {
		perror(path);
		last = sh_err_to_exit_status(SH_ERR_IO);
	}
	free(line);
	fclose(f);
	return last;
}

static void usage(const char *argv0)
{
	fprintf(stderr, "usage: %s [-c command] [script-file]\n", argv0);
}

/* ---- interactive line editing, completion and command history ---- */

/* Bounded, per-session history: nothing is written to disk. */
#define SH_HISTORY_MAX 128

enum sh_edit_status {
	SH_EDIT_OK = 0,
	SH_EDIT_EOF,
	SH_EDIT_CANCEL,
	SH_EDIT_ERR,
};

enum sh_tty_status {
	SH_TTY_EOF = -1,
	SH_TTY_ERR = -2,
	SH_TTY_TIMEOUT = -3,
};

struct sh_history {
	char **items;
	size_t len;
	size_t cap;
};

struct sh_line {
	char *data;
	size_t len;
	size_t cap;
	size_t pos;
};

struct sh_edit {
	const char *prompt;
	size_t prompt_len;
	struct sh_line line;
	size_t hist_index;
	char *draft;
	int tab_armed;
};

static struct sh_history sh_history;
static struct termios sh_tty_saved;
static volatile int sh_tty_is_raw;

static int sh_write_all(int fd, const char *buf, size_t len)
{
	while (len > 0) {
		ssize_t n = write(fd, buf, len);

		if (n < 0) {
			if (errno == EINTR || errno == EBUSY)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

static void sh_history_add(const char *line)
{
	const char *p;
	char *copy;

	for (p = line; *p; p++)
		if (!isspace((unsigned char)*p))
			break;
	if (!*p)
		return;

	copy = strdup(line);
	if (!copy)
		return;

	if (sh_history.len == SH_HISTORY_MAX) {
		free(sh_history.items[0]);
		memmove(sh_history.items, sh_history.items + 1,
			(sh_history.len - 1) * sizeof(*sh_history.items));
		sh_history.len--;
	}

	if (sh_history.len == sh_history.cap) {
		size_t cap = sh_history.cap ? sh_history.cap * 2 : 16;
		char **items;

		if (cap > SH_HISTORY_MAX)
			cap = SH_HISTORY_MAX;
		items = realloc(sh_history.items, cap * sizeof(*items));
		if (!items) {
			free(copy);
			return;
		}
		sh_history.items = items;
		sh_history.cap = cap;
	}

	sh_history.items[sh_history.len++] = copy;
}

static int sh_line_reserve(struct sh_line *line, size_t need)
{
	size_t cap;
	char *data;

	if (need <= line->cap)
		return 0;

	cap = line->cap ? line->cap : 64;
	while (cap < need) {
		if (cap > (size_t)-1 / 2) {
			cap = need;
			break;
		}
		cap *= 2;
	}

	data = realloc(line->data, cap);
	if (!data)
		return -1;
	line->data = data;
	line->cap = cap;
	return 0;
}

static int sh_line_set(struct sh_line *line, const char *s)
{
	size_t len = strlen(s);

	if (sh_line_reserve(line, len + 1))
		return -1;
	memcpy(line->data, s, len + 1);
	line->len = len;
	line->pos = len;
	return 0;
}

static int sh_line_insert(struct sh_line *line, char c)
{
	if (sh_line_reserve(line, line->len + 2))
		return -1;

	memmove(line->data + line->pos + 1, line->data + line->pos,
		line->len - line->pos);
	line->data[line->pos++] = c;
	line->len++;
	line->data[line->len] = '\0';
	return 0;
}

static void sh_line_delete_before(struct sh_line *line)
{
	if (line->pos == 0)
		return;
	memmove(line->data + line->pos - 1, line->data + line->pos,
		line->len - line->pos + 1);
	line->pos--;
	line->len--;
}

static void sh_line_delete_at(struct sh_line *line)
{
	if (line->pos >= line->len)
		return;
	memmove(line->data + line->pos, line->data + line->pos + 1,
		line->len - line->pos);
	line->len--;
}

static void sh_line_kill_to_end(struct sh_line *line)
{
	line->len = line->pos;
	line->data[line->len] = '\0';
}

static void sh_line_kill_to_start(struct sh_line *line)
{
	if (line->pos == 0)
		return;
	memmove(line->data, line->data + line->pos, line->len - line->pos + 1);
	line->len -= line->pos;
	line->pos = 0;
}

static void sh_line_erase_word(struct sh_line *line)
{
	size_t start = line->pos;

	while (start > 0 && isspace((unsigned char)line->data[start - 1]))
		start--;
	while (start > 0 && !isspace((unsigned char)line->data[start - 1]))
		start--;
	memmove(line->data + start, line->data + line->pos,
		line->len - line->pos + 1);
	line->len -= line->pos - start;
	line->pos = start;
}

static int sh_edit_history_move(struct sh_edit *ed, int direction)
{
	char *draft;

	if (sh_history.len == 0)
		return 0;

	if (direction < 0) {
		if (ed->hist_index == 0)
			return 0;
		if (ed->hist_index == sh_history.len) {
			draft = strdup(ed->line.data ? ed->line.data : "");
			if (!draft)
				return -1;
			free(ed->draft);
			ed->draft = draft;
		}
		ed->hist_index--;
		return sh_line_set(&ed->line, sh_history.items[ed->hist_index]);
	}

	if (ed->hist_index >= sh_history.len)
		return 0;
	ed->hist_index++;
	if (ed->hist_index == sh_history.len)
		return sh_line_set(&ed->line, ed->draft ? ed->draft : "");
	return sh_line_set(&ed->line, sh_history.items[ed->hist_index]);
}

#define SH_COMPLETE_PATH_MAX 1024

struct sh_match {
	char *name;
	int is_dir;
};

struct sh_match_list {
	struct sh_match *items;
	size_t len;
	size_t cap;
};

struct sh_complete_ctx {
	size_t start;
	int cmd_pos;
	int valid;
};

static void sh_match_list_free(struct sh_match_list *list)
{
	size_t i;

	for (i = 0; i < list->len; i++)
		free(list->items[i].name);
	free(list->items);
	list->items = NULL;
	list->len = 0;
	list->cap = 0;
}

static int sh_match_add(struct sh_match_list *list, const char *name,
			int is_dir)
{
	size_t i;
	char *copy;

	for (i = 0; i < list->len; i++)
		if (!strcmp(list->items[i].name, name))
			return 0;

	if (list->len == list->cap) {
		size_t cap = list->cap ? list->cap * 2 : 16;
		struct sh_match *items =
			realloc(list->items, cap * sizeof(*items));

		if (!items)
			return -1;
		list->items = items;
		list->cap = cap;
	}

	copy = strdup(name);
	if (!copy)
		return -1;
	list->items[list->len].name = copy;
	list->items[list->len].is_dir = is_dir;
	list->len++;
	return 0;
}

static int sh_complete_cmp(const void *a, const void *b)
{
	const struct sh_match *ma = a;
	const struct sh_match *mb = b;

	return strcmp(ma->name, mb->name);
}

static int sh_join_path(char *out, size_t cap, const char *dir,
			const char *name)
{
	int n;

	if (!dir[0])
		n = snprintf(out, cap, "%s", name);
	else if (dir[strlen(dir) - 1] == '/')
		n = snprintf(out, cap, "%s%s", dir, name);
	else
		n = snprintf(out, cap, "%s/%s", dir, name);
	return n < 0 || (size_t)n >= cap ? -1 : 0;
}

static int sh_complete_dir_name(const char *dirpart, char *out, size_t cap)
{
	size_t len = strlen(dirpart);

	if (len == 0) {
		if (cap < 2)
			return -1;
		out[0] = '.';
		out[1] = '\0';
		return 0;
	}

	while (len > 1 && dirpart[len - 1] == '/')
		len--;
	if (len >= cap)
		return -1;
	memcpy(out, dirpart, len);
	out[len] = '\0';
	return 0;
}

static int sh_complete_scan_dir(struct sh_match_list *list, const char *dir,
				const char *name_prefix, const char *base,
				int command_mode)
{
	char dbuf[4096];
	size_t base_len = strlen(base);
	int fd;

	fd = open(dir, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return 0;

	for (;;) {
		ssize_t n = getdents64(fd, dbuf, sizeof(dbuf));
		size_t pos;

		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			break;

		for (pos = 0; pos < (size_t)n;) {
			struct dirent64 *d = (struct dirent64 *)(dbuf + pos);
			const char *name;
			char full[SH_COMPLETE_PATH_MAX];
			char match[SH_COMPLETE_PATH_MAX];
			struct stat st;

			if (d->d_reclen == 0)
				break;
			pos += d->d_reclen;

			name = d->d_name;
			if (!strcmp(name, ".") || !strcmp(name, ".."))
				continue;
			if (base[0] != '.' && name[0] == '.')
				continue;
			if (strncmp(name, base, base_len))
				continue;
			if (sh_join_path(full, sizeof(full), dir, name))
				continue;
			if (stat(full, &st))
				continue;

			if (command_mode) {
				if (!S_ISREG(st.st_mode) ||
				    access(full, X_OK) != 0)
					continue;
				if (sh_match_add(list, name, 0))
					goto fail;
			} else {
				if (sh_join_path(match, sizeof(match),
						 name_prefix, name))
					continue;
				if (sh_match_add(list, match,
						 S_ISDIR(st.st_mode)))
					goto fail;
			}
		}
	}

	close(fd);
	return 0;
fail:
	close(fd);
	return -1;
}

static int sh_complete_context(const struct sh_line *line,
			       struct sh_complete_ctx *ctx)
{
	size_t pos = line->pos;
	size_t start = pos;
	size_t i = 0;
	int at_cmd = 1;
	int in_word = 0;
	int word_cmd = 0;
	int quoted = 0;
	char quote = 0;

	while (i < pos) {
		char c = line->data[i];

		if (quote) {
			if (quote == '"' && c == '\\' && i + 1 < pos) {
				i += 2;
				continue;
			}
			if (c == quote)
				quote = 0;
			i++;
			continue;
		}

		if (c == '\\') {
			if (i + 1 < pos)
				i += 2;
			else
				i++;
			continue;
		}

		if (c == '\'' || c == '"') {
			if (!in_word) {
				in_word = 1;
				start = i;
				word_cmd = at_cmd;
			}
			quoted = 1;
			quote = c;
			i++;
			continue;
		}

		if (isspace((unsigned char)c)) {
			if (in_word) {
				in_word = 0;
				quoted = 0;
				at_cmd = 0;
			}
			i++;
			continue;
		}

		if (c == '|') {
			in_word = 0;
			quoted = 0;
			at_cmd = 1;
			i++;
			continue;
		}

		if (c == '<' || c == '>') {
			in_word = 0;
			quoted = 0;
			at_cmd = 0;
			i++;
			continue;
		}

		if (c == '#')
			return 0;

		if (!in_word) {
			in_word = 1;
			start = i;
			word_cmd = at_cmd;
		}
		i++;
	}

	ctx->start = in_word ? start : pos;
	ctx->cmd_pos = in_word ? word_cmd : at_cmd;
	ctx->valid = !in_word || (!quoted && quote == 0);
	return ctx->valid;
}

static int sh_complete_word_text(const struct sh_line *line, size_t start,
				 char **out)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;
	size_t i;

	for (i = start; i < line->pos; i++) {
		char c = line->data[i];

		if (c == '\\' && i + 1 < line->pos)
			c = line->data[++i];
		buf = buf_push(buf, &len, &cap, c);
		if (!buf)
			return -1;
	}

	buf = buf_push(buf, &len, &cap, '\0');
	if (!buf)
		return -1;
	*out = buf;
	return 0;
}

static int sh_complete_escape(const char *text, char suffix, char **out)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;

	for (; *text; text++) {
		char c = *text;

		if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
		    c == '|' || c == '<' || c == '>' || c == '#' || c == '\\' ||
		    c == '\'' || c == '"') {
			buf = buf_push(buf, &len, &cap, '\\');
			if (!buf)
				return -1;
		}
		buf = buf_push(buf, &len, &cap, c);
		if (!buf)
			return -1;
	}

	if (suffix) {
		buf = buf_push(buf, &len, &cap, suffix);
		if (!buf)
			return -1;
	}

	buf = buf_push(buf, &len, &cap, '\0');
	if (!buf)
		return -1;
	*out = buf;
	return 0;
}

static size_t sh_complete_lcp(const struct sh_match_list *list)
{
	const char *first = list->items[0].name;
	size_t lcp = strlen(first);
	size_t i;

	for (i = 1; i < list->len; i++) {
		const char *name = list->items[i].name;
		size_t j = 0;

		while (j < lcp && name[j] == first[j])
			j++;
		lcp = j;
		if (lcp == 0)
			break;
	}
	return lcp;
}

static const char *sh_complete_display_name(const struct sh_match *match)
{
	const char *slash = strrchr(match->name, '/');

	return slash ? slash + 1 : match->name;
}

static void sh_complete_print(const struct sh_match_list *list)
{
	struct winsize ws;
	size_t cols = 80;
	size_t maxlen = 0;
	size_t width;
	size_t ncols;
	size_t rows;
	size_t i, r, c;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col)
		cols = ws.ws_col;

	for (i = 0; i < list->len; i++) {
		size_t len = strlen(sh_complete_display_name(&list->items[i]));

		if (list->items[i].is_dir)
			len++;
		if (len > maxlen)
			maxlen = len;
	}

	width = maxlen + 2;
	ncols = cols / width;
	if (ncols < 1)
		ncols = 1;
	if (ncols > list->len)
		ncols = list->len;
	rows = (list->len + ncols - 1) / ncols;

	(void)sh_write_all(STDOUT_FILENO, "\r\n", 2);
	for (r = 0; r < rows; r++) {
		for (c = 0; c < ncols; c++) {
			size_t idx = c * rows + r;
			const struct sh_match *match;
			const char *name;
			size_t len;

			if (idx >= list->len)
				continue;

			match = &list->items[idx];
			name = sh_complete_display_name(match);
			len = strlen(name);
			(void)sh_write_all(STDOUT_FILENO, name, len);
			if (match->is_dir) {
				(void)sh_write_all(STDOUT_FILENO, "/", 1);
				len++;
			}
			if (c + 1 < ncols && idx + rows < list->len) {
				size_t pad = width - len;

				while (pad-- > 0)
					(void)sh_write_all(STDOUT_FILENO, " ",
							   1);
			}
		}
		(void)sh_write_all(STDOUT_FILENO, "\r\n", 2);
	}
}

static int sh_complete_collect(const struct sh_line *line,
			       const struct sh_complete_ctx *ctx,
			       struct sh_match_list *list)
{
	static const char *const path_dirs[] = {
		"/bin",
		"/sbin",
		"/usr/bin",
		"/usr/sbin",
	};
	static const char *const builtins[] = { "cd", "pwd", "exit" };
	char *word = NULL;
	const char *slash;
	size_t i;
	int rc = 0;

	if (sh_complete_word_text(line, ctx->start, &word))
		return -1;

	slash = strrchr(word, '/');
	if (ctx->cmd_pos && !slash) {
		size_t word_len = strlen(word);

		for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
			if (!strncmp(builtins[i], word, word_len) &&
			    sh_match_add(list, builtins[i], 0)) {
				rc = -1;
				goto out;
			}
		}
		for (i = 0; i < sizeof(path_dirs) / sizeof(path_dirs[0]); i++) {
			if (sh_complete_scan_dir(list, path_dirs[i], "", word,
						 1)) {
				rc = -1;
				goto out;
			}
		}
	} else {
		size_t dirlen = slash ? (size_t)(slash - word) + 1 : 0;
		const char *base = word + dirlen;
		char *dirpart;
		char dir[SH_COMPLETE_PATH_MAX];

		dirpart = malloc(dirlen + 1);
		if (!dirpart) {
			rc = -1;
			goto out;
		}
		memcpy(dirpart, word, dirlen);
		dirpart[dirlen] = '\0';

		if (sh_complete_dir_name(dirpart, dir, sizeof(dir)) == 0 &&
		    sh_complete_scan_dir(list, dir, dirpart, base, 0))
			rc = -1;
		free(dirpart);
	}

out:
	free(word);
	if (rc == 0 && list->len > 1)
		qsort(list->items, list->len, sizeof(list->items[0]),
		      sh_complete_cmp);
	return rc;
}

static int sh_line_replace(struct sh_line *line, size_t start, size_t end,
			   const char *text)
{
	size_t text_len = strlen(text);
	size_t new_len;

	if (start > end || end > line->len)
		return -1;

	new_len = line->len - (end - start) + text_len;
	if (sh_line_reserve(line, new_len + 1))
		return -1;

	memmove(line->data + start + text_len, line->data + end,
		line->len - end + 1);
	memcpy(line->data + start, text, text_len);
	line->len = new_len;
	line->pos = start + text_len;
	return 0;
}

static int sh_edit_complete(struct sh_edit *ed)
{
	struct sh_complete_ctx ctx;
	struct sh_match_list list = { 0 };
	char *word = NULL;
	char *replacement = NULL;
	size_t word_len;
	int rc = 0;

	if (!sh_complete_context(&ed->line, &ctx))
		return 0;
	if (sh_complete_word_text(&ed->line, ctx.start, &word))
		return -1;
	word_len = strlen(word);

	if (sh_complete_collect(&ed->line, &ctx, &list)) {
		rc = -1;
		goto out;
	}

	if (list.len == 0) {
		ed->tab_armed = 0;
		(void)sh_write_all(STDOUT_FILENO, "\a", 1);
		goto out;
	}

	if (list.len == 1) {
		char suffix = list.items[0].is_dir ? '/' : ' ';

		if (sh_complete_escape(list.items[0].name, suffix,
				       &replacement)) {
			rc = -1;
			goto out;
		}
		if (sh_line_replace(&ed->line, ctx.start, ed->line.pos,
				    replacement)) {
			rc = -1;
			goto out;
		}
		ed->tab_armed = 0;
		goto out;
	}

	size_t lcp = sh_complete_lcp(&list);

	if (lcp > word_len) {
		char *prefix = strndup(list.items[0].name, lcp);

		if (!prefix) {
			rc = -1;
			goto out;
		}
		if (sh_complete_escape(prefix, '\0', &replacement)) {
			free(prefix);
			rc = -1;
			goto out;
		}
		free(prefix);
		if (sh_line_replace(&ed->line, ctx.start, ed->line.pos,
				    replacement)) {
			rc = -1;
			goto out;
		}
		ed->tab_armed = 0;
	} else if (ed->tab_armed) {
		sh_complete_print(&list);
		ed->tab_armed = 0;
	} else {
		ed->tab_armed = 1;
	}

out:
	free(word);
	free(replacement);
	sh_match_list_free(&list);
	return rc;
}

static int sh_tty_raw(void)
{
	struct termios raw;

	if (ioctl(STDIN_FILENO, TCGETS, &sh_tty_saved) != 0)
		return -1;

	raw = sh_tty_saved;
	raw.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
	raw.c_iflag &= ~(ICRNL | INLCR | IGNCR | IXON | IXOFF);
	raw.c_oflag &= ~OPOST;
	raw.c_cc[VMIN] = 1;
	raw.c_cc[VTIME] = 0;

	if (ioctl(STDIN_FILENO, TCSETS, &raw) != 0)
		return -1;

	sh_tty_is_raw = 1;
	return 0;
}

static void sh_tty_restore(void)
{
	if (!sh_tty_is_raw)
		return;
	(void)ioctl(STDIN_FILENO, TCSETS, &sh_tty_saved);
	sh_tty_is_raw = 0;
}

static void sh_tty_fatal(int sig)
{
	sh_tty_restore();
	_exit(128 + sig);
}

static int sh_tty_getc(void)
{
	for (;;) {
		unsigned char c;
		ssize_t n = read(STDIN_FILENO, &c, 1);

		if (n == 1)
			return c;
		if (n == 0)
			return SH_TTY_EOF;
		if (errno == EINTR)
			continue;
		return SH_TTY_ERR;
	}
}

static int sh_tty_getc_timeout(int timeout_ms)
{
	struct pollfd pfd;
	int ret;

	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;
	pfd.revents = 0;
	do {
		ret = poll(&pfd, 1, timeout_ms);
	} while (ret < 0 && errno == EINTR);

	if (ret <= 0)
		return SH_TTY_TIMEOUT;
	return sh_tty_getc();
}

static int sh_edit_escape_seq(struct sh_edit *ed)
{
	char params[16];
	size_t nparams = 0;
	int final = -1;
	int c;

	for (;;) {
		c = sh_tty_getc_timeout(50);
		if (c < 0)
			return 0;
		if (c >= 0x40 && c <= 0x7e) {
			final = c;
			break;
		}
		if (nparams + 1 < sizeof(params))
			params[nparams++] = (char)c;
	}
	params[nparams] = '\0';

	switch (final) {
	case 'A':
		return sh_edit_history_move(ed, -1);
	case 'B':
		return sh_edit_history_move(ed, 1);
	case 'C':
		if (ed->line.pos < ed->line.len)
			ed->line.pos++;
		return 0;
	case 'D':
		if (ed->line.pos > 0)
			ed->line.pos--;
		return 0;
	case 'H':
		ed->line.pos = 0;
		return 0;
	case 'F':
		ed->line.pos = ed->line.len;
		return 0;
	case '~': {
		int code = atoi(params);

		switch (code) {
		case 1:
		case 7:
			ed->line.pos = 0;
			break;
		case 4:
		case 8:
			ed->line.pos = ed->line.len;
			break;
		case 3:
			sh_line_delete_at(&ed->line);
			break;
		default:
			break;
		}
		return 0;
	}
	default:
		return 0;
	}
}

static int sh_edit_escape(struct sh_edit *ed)
{
	int c = sh_tty_getc_timeout(50);

	if (c == '[' || c == 'O')
		return sh_edit_escape_seq(ed);
	return 0;
}

static int sh_edit_draw(const struct sh_edit *ed)
{
	char seq[32];
	int n;

	if (sh_write_all(STDOUT_FILENO, "\r\033[K", 4))
		return -1;
	if (sh_write_all(STDOUT_FILENO, ed->prompt, ed->prompt_len))
		return -1;
	if (ed->line.len &&
	    sh_write_all(STDOUT_FILENO, ed->line.data, ed->line.len))
		return -1;

	if (ed->line.pos < ed->line.len) {
		n = snprintf(seq, sizeof(seq), "\033[%luD",
			     (unsigned long)(ed->line.len - ed->line.pos));
		if (n < 0)
			return -1;
		if ((size_t)n >= sizeof(seq))
			n = (int)sizeof(seq) - 1;
		if (sh_write_all(STDOUT_FILENO, seq, (size_t)n))
			return -1;
	}
	return 0;
}

static int sh_edit_init(struct sh_edit *ed, const char *prompt,
			size_t prompt_len)
{
	memset(ed, 0, sizeof(*ed));
	ed->prompt = prompt;
	ed->prompt_len = prompt_len;
	ed->hist_index = sh_history.len;

	if (sh_line_reserve(&ed->line, 1))
		return -1;
	ed->line.data[0] = '\0';
	return 0;
}

static void sh_edit_free(struct sh_edit *ed)
{
	free(ed->line.data);
	free(ed->draft);
	ed->line.data = NULL;
	ed->draft = NULL;
}

static int sh_edit_line(const char *prompt, size_t prompt_len, char **out)
{
	struct sh_edit ed;
	int status = SH_EDIT_OK;

	*out = NULL;
	if (sh_edit_init(&ed, prompt, prompt_len))
		return SH_EDIT_ERR;
	if (sh_edit_draw(&ed)) {
		sh_edit_free(&ed);
		return SH_EDIT_ERR;
	}

	for (;;) {
		int c = sh_tty_getc();

		if (c == SH_TTY_EOF) {
			if (sh_write_all(STDOUT_FILENO, "\r\n", 2))
				status = SH_EDIT_ERR;
			else
				status = SH_EDIT_EOF;
			break;
		}
		if (c == SH_TTY_ERR) {
			status = SH_EDIT_ERR;
			break;
		}

		if (c != '\t')
			ed.tab_armed = 0;

		if (c == '\n' || c == '\r') {
			if (sh_write_all(STDOUT_FILENO, "\r\n", 2)) {
				status = SH_EDIT_ERR;
			} else {
				*out = ed.line.data;
				ed.line.data = NULL;
			}
			break;
		}
		if (c == 0x03) {
			(void)sh_write_all(STDOUT_FILENO, "^C\r\n", 4);
			status = SH_EDIT_CANCEL;
			break;
		}

		if (c == 0x04) {
			if (ed.line.len == 0) {
				(void)sh_write_all(STDOUT_FILENO, "\r\n", 2);
				status = SH_EDIT_EOF;
				break;
			}
			sh_line_delete_at(&ed.line);
		} else if (c == 0x7f || c == 0x08) {
			sh_line_delete_before(&ed.line);
		} else if (c == 0x01) {
			ed.line.pos = 0;
		} else if (c == 0x05) {
			ed.line.pos = ed.line.len;
		} else if (c == 0x02) {
			if (ed.line.pos > 0)
				ed.line.pos--;
		} else if (c == 0x06) {
			if (ed.line.pos < ed.line.len)
				ed.line.pos++;
		} else if (c == '\t') {
			if (sh_edit_complete(&ed)) {
				status = SH_EDIT_ERR;
				break;
			}
		} else if (c == 0x0b) {
			sh_line_kill_to_end(&ed.line);
		} else if (c == 0x15) {
			sh_line_kill_to_start(&ed.line);
		} else if (c == 0x17) {
			sh_line_erase_word(&ed.line);
		} else if (c == 0x10) {
			if (sh_edit_history_move(&ed, -1)) {
				status = SH_EDIT_ERR;
				break;
			}
		} else if (c == 0x0e) {
			if (sh_edit_history_move(&ed, 1)) {
				status = SH_EDIT_ERR;
				break;
			}
		} else if (c == 0x1b) {
			if (sh_edit_escape(&ed)) {
				status = SH_EDIT_ERR;
				break;
			}
		} else if (c >= 0x20 && c != 0x7f) {
			if (sh_line_insert(&ed.line, (char)c)) {
				status = SH_EDIT_ERR;
				break;
			}
		} else {
			continue;
		}

		if (sh_edit_draw(&ed)) {
			status = SH_EDIT_ERR;
			break;
		}
	}

	sh_edit_free(&ed);
	return status;
}

static char sh_prompt_char(void)
{
	static char prompt_char;

	if (!prompt_char)
		prompt_char = geteuid() == 0 ? '#' : '$';
	return prompt_char;
}

static int run_interactive_tty(void)
{
	char prompt[sizeof(cwd) + 8];
	int last = 0;

	signal(SIGHUP, sh_tty_fatal);
	signal(SIGTERM, sh_tty_fatal);

	for (;;) {
		char *line = NULL;
		int n;
		int r;

		n = snprintf(prompt, sizeof(prompt), "%s %c ", cwd,
			     sh_prompt_char());
		if (n < 0) {
			perror("sh: prompt");
			return sh_err_to_exit_status(SH_ERR_IO);
		}
		if ((size_t)n >= sizeof(prompt))
			n = (int)sizeof(prompt) - 1;

		if (fflush(stdout)) {
			perror("sh: fflush");
			return sh_err_to_exit_status(SH_ERR_IO);
		}
		if (sh_tty_raw() < 0) {
			perror("sh: terminal");
			return sh_err_to_exit_status(SH_ERR_IO);
		}

		r = sh_edit_line(prompt, (size_t)n, &line);
		sh_tty_restore();

		if (r == SH_EDIT_CANCEL) {
			free(line);
			continue;
		}
		if (r == SH_EDIT_EOF) {
			free(line);
			break;
		}
		if (r == SH_EDIT_ERR) {
			free(line);
			perror("sh: read line");
			return sh_err_to_exit_status(SH_ERR_IO);
		}

		sh_history_add(line);
		last = run_line(line, strlen(line));
		free(line);
	}
	return last;
}

static int run_interactive_getline(void)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t nread;
	int last = 0;

	for (;;) {
		printf("%s %c ", cwd, sh_prompt_char());
		fflush(stdout);
		nread = getline(&line, &cap, stdin);
		if (nread == -1) {
			if (ferror(stdin)) {
				perror("sh");
				free(line);
				return sh_err_to_exit_status(SH_ERR_IO);
			}
			break;
		}
		chomp_crlf(line);
		sh_history_add(line);
		last = run_line(line, strlen(line));
	}
	free(line);
	return last;
}

static int run_interactive(void)
{
	struct termios t;

	if (!getcwd(cwd, sizeof(cwd) - 1)) {
		perror("sh: getcwd");
		return sh_err_to_exit_status(SH_ERR_IO);
	}
	cwd[sizeof(cwd) - 1] = '\0';

	if (ioctl(STDIN_FILENO, TCGETS, &t) == 0 &&
	    ioctl(STDOUT_FILENO, TCGETS, &t) == 0)
		return run_interactive_tty();

	return run_interactive_getline();
}

int main(int argc, char **argv)
{
	/*
	 * The tty core delivers SIGINT/SIGQUIT to the whole foreground
	 * process group.  The shell stays in that group (no job control), so
	 * it must not die together with the running command.
	 */
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);

	if (argc >= 2 && strcmp(argv[1], "-c") == 0) {
		if (argc < 3) {
			usage(argv[0]);
			return 2;
		}
		return run_line(argv[2], strlen(argv[2]));
	}

	if (argc >= 2 && strcmp(argv[1], "-h") == 0) {
		usage(argv[0]);
		return 0;
	}

	if (argc >= 2)
		return run_script(argv[1]);

	return run_interactive();
}
