#include "sh_err.h"
#include "sh_lexer.h"
#include "sh_parser.h"
#include "sh_pipeline.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
