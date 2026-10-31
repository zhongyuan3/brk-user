#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

static int run_interactive(void)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t nread;
	int last = 0;

	if (!getcwd(cwd, sizeof(cwd) - 1)) {
		perror("sh: getcwd");
		return sh_err_to_exit_status(SH_ERR_IO);
	}
	cwd[sizeof(cwd) - 1] = '\0';
	for (;;) {
		printf("%s $ ", cwd);
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
		last = run_line(line, strlen(line));
	}
	free(line);
	return last;
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
