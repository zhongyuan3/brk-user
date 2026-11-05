#include "sh.h"

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
			sh_builtin_exit(&pl);
		if (!strcmp(a0, "cd")) {
			err = sh_builtin_cd(&pl);
			sh_pipeline_release(&pl);
			if (err != SH_OK)
				return sh_err_to_exit_status(err);
			return 0;
		}
		if (!strcmp(a0, "pwd")) {
			sh_builtin_pwd();
			sh_pipeline_release(&pl);
			return 0;
		}
		if (!strcmp(a0, "alias")) {
			int status = sh_builtin_alias(&pl);

			sh_pipeline_release(&pl);
			return status;
		}
		if (!strcmp(a0, "export")) {
			int status = sh_builtin_export(&pl);

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

static void chomp_crlf(char *s)
{
	size_t n = strlen(s);

	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
		s[--n] = '\0';
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
