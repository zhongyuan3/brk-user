#include "sh.h"

#include <sys/utsname.h>

#define SH_PROMPT_MAX 1024

static int sh_is_builtin(const char *name)
{
	return !strcmp(name, "exit") || !strcmp(name, "cd") ||
	       !strcmp(name, "pwd") || !strcmp(name, "alias") ||
	       !strcmp(name, "export") || !strcmp(name, "unset");
}

static int sh_is_special_builtin(const char *name)
{
	return !strcmp(name, "exit") || !strcmp(name, "export") ||
	       !strcmp(name, "unset");
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
		if (sh_cmd_extract_assignments(&pl.cmds[i]) != 0) {
			fprintf(stderr, "sh: out of memory\n");
			sh_pipeline_release(&pl);
			return 1;
		}
		if (sh_alias_expand_cmd(&pl.cmds[i]) != 0) {
			fprintf(stderr, "sh: alias expansion failed\n");
			sh_pipeline_release(&pl);
			return 1;
		}
		/* An alias value may itself start with NAME=VALUE words. */
		if (sh_cmd_extract_assignments(&pl.cmds[i]) != 0) {
			fprintf(stderr, "sh: out of memory\n");
			sh_pipeline_release(&pl);
			return 1;
		}
	}

	for (size_t i = 0; i < pl.nr_cmds; i++) {
		if (pl.cmds[i].argc > 0)
			continue;
		if (pl.nr_cmds == 1 && pl.cmds[0].nr_assign > 0) {
			int status = 0;

			if (sh_apply_assignments(&pl.cmds[0]) != 0) {
				fprintf(stderr, "sh: setenv: %s\n",
					strerror(errno));
				status = 1;
			}
			sh_pipeline_release(&pl);
			return status;
		}
		fprintf(stderr, "sh: missing command\n");
		sh_pipeline_release(&pl);
		return 1;
	}

	if (pl.nr_cmds == 1 && pl.cmds[0].argv[0]) {
		struct sh_simple_cmd *cmd = &pl.cmds[0];
		const char *a0 = cmd->argv[0];
		struct sh_env_save *saves = NULL;
		size_t nr_saves = 0;
		int status = 0;

		if (sh_is_builtin(a0) && cmd->nr_assign > 0) {
			if (!sh_is_special_builtin(a0) &&
			    sh_env_save_assignments(cmd, &saves, &nr_saves) !=
				    0) {
				fprintf(stderr, "sh: out of memory\n");
				sh_pipeline_release(&pl);
				return 1;
			}
			if (sh_apply_assignments(cmd) != 0) {
				fprintf(stderr, "sh: setenv: %s\n",
					strerror(errno));
				sh_env_restore(saves, nr_saves);
				sh_pipeline_release(&pl);
				return 1;
			}
		}

		if (!strcmp(a0, "exit"))
			sh_builtin_exit(&pl);
		if (!strcmp(a0, "cd")) {
			err = sh_builtin_cd(&pl);
			sh_env_restore(saves, nr_saves);
			sh_pipeline_release(&pl);
			if (err != SH_OK)
				return sh_err_to_exit_status(err);
			return 0;
		}
		if (!strcmp(a0, "pwd")) {
			sh_builtin_pwd();
			sh_env_restore(saves, nr_saves);
			sh_pipeline_release(&pl);
			return 0;
		}
		if (!strcmp(a0, "alias")) {
			status = sh_builtin_alias(&pl);
			sh_env_restore(saves, nr_saves);
			sh_pipeline_release(&pl);
			return status;
		}
		if (!strcmp(a0, "export")) {
			status = sh_builtin_export(&pl);
			sh_pipeline_release(&pl);
			return status;
		}
		if (!strcmp(a0, "unset")) {
			status = sh_builtin_unset(&pl);
			sh_pipeline_release(&pl);
			return status;
		}

		sh_env_restore(saves, nr_saves);
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

static void run_profile(const char *path)
{
	FILE *f = fopen(path, "r");
	char *line = NULL;
	size_t cap = 0;

	if (!f)
		return;

	while (getline(&line, &cap, f) != -1) {
		chomp_crlf(line);
		(void)run_line(line, strlen(line));
	}
	free(line);
	fclose(f);
}

static void run_login_profiles(void)
{
	const char *home = getenv("HOME");
	char path[1024];

	run_profile("/etc/profile");
	if (!home || !home[0])
		return;

	int n = snprintf(path, sizeof(path), "%s/.profile", home);

	if (n > 0 && (size_t)n < sizeof(path))
		run_profile(path);
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

static void prompt_append(char *buf, size_t size, size_t *len, const char *text)
{
	size_t n = strlen(text);

	if (*len >= size - 1)
		return;
	if (n > size - 1 - *len)
		n = size - 1 - *len;
	memcpy(buf + *len, text, n);
	*len += n;
	buf[*len] = '\0';
}

static void prompt_append_char(char *buf, size_t size, size_t *len, char c)
{
	char text[2] = { c, '\0' };

	prompt_append(buf, size, len, text);
}

static void prompt_append_cwd(char *buf, size_t size, size_t *len, int base)
{
	const char *home = getenv("HOME");

	if (!base && home && home[0]) {
		size_t home_len = strlen(home);

		if (!strncmp(cwd, home, home_len) &&
		    (cwd[home_len] == '\0' || cwd[home_len] == '/')) {
			prompt_append(buf, size, len, "~");
			prompt_append(buf, size, len, cwd + home_len);
			return;
		}
	}

	if (base) {
		const char *slash = strrchr(cwd, '/');
		const char *name = slash ? slash + 1 : cwd;

		if (!name[0])
			name = "/";
		prompt_append(buf, size, len, name);
	} else {
		prompt_append(buf, size, len, cwd);
	}
}

static int sh_build_prompt(char *buf, size_t size)
{
	const char *ps1 = getenv("PS1");
	size_t len = 0;

	if (!size)
		return -1;
	buf[0] = '\0';

	if (!ps1) {
		prompt_append(buf, size, &len, cwd);
		prompt_append(buf, size, &len, " ");
		prompt_append_char(buf, size, &len, sh_prompt_char());
		prompt_append(buf, size, &len, " ");
		return (int)len;
	}

	for (const char *p = ps1; *p; p++) {
		if (*p != '\\') {
			prompt_append_char(buf, size, &len, *p);
			continue;
		}

		p++;
		if (!*p) {
			prompt_append_char(buf, size, &len, '\\');
			break;
		}

		switch (*p) {
		case 'u': {
			const char *user = getenv("USER");

			if (!user || !user[0])
				user = getenv("LOGNAME");
			if (!user || !user[0])
				user = "?";
			prompt_append(buf, size, &len, user);
			break;
		}
		case 'h': {
			struct utsname u;

			if (uname(&u) == 0)
				prompt_append(buf, size, &len, u.nodename);
			break;
		}
		case 'w':
			prompt_append_cwd(buf, size, &len, 0);
			break;
		case 'W':
			prompt_append_cwd(buf, size, &len, 1);
			break;
		case '$':
			prompt_append_char(buf, size, &len, sh_prompt_char());
			break;
		case 'n':
			prompt_append_char(buf, size, &len, '\n');
			break;
		case '\\':
			prompt_append_char(buf, size, &len, '\\');
			break;
		default:
			prompt_append_char(buf, size, &len, '\\');
			prompt_append_char(buf, size, &len, *p);
			break;
		}
	}

	return (int)len;
}

static int run_interactive_tty(void)
{
	char prompt[SH_PROMPT_MAX];
	int last = 0;

	signal(SIGHUP, sh_tty_fatal);
	signal(SIGTERM, sh_tty_fatal);

	for (;;) {
		char *line = NULL;
		int n;
		int r;

		n = sh_build_prompt(prompt, sizeof(prompt));
		if (n < 0) {
			perror("sh: prompt");
			return sh_err_to_exit_status(SH_ERR_IO);
		}

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
	char prompt[SH_PROMPT_MAX];
	size_t cap = 0;
	ssize_t nread;
	int last = 0;

	for (;;) {
		if (sh_build_prompt(prompt, sizeof(prompt)) < 0) {
			perror("sh: prompt");
			free(line);
			return sh_err_to_exit_status(SH_ERR_IO);
		}
		fputs(prompt, stdout);
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
	const char *term = getenv("TERM");

	if (!term || !term[0] || !strcmp(term, "dumb"))
		return run_interactive_getline();

	if (ioctl(STDIN_FILENO, TCGETS, &t) == 0 &&
	    ioctl(STDOUT_FILENO, TCGETS, &t) == 0)
		return run_interactive_tty();

	return run_interactive_getline();
}

static int init_shell_cwd(void)
{
	if (!getcwd(cwd, sizeof(cwd) - 1)) {
		perror("sh: getcwd");
		return -1;
	}
	cwd[sizeof(cwd) - 1] = '\0';

	if (setenv("PWD", cwd, 1) != 0) {
		perror("sh: PWD");
		return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	int login_shell = argc > 0 && argv[0] && argv[0][0] == '-';

	/*
	 * The tty core delivers SIGINT/SIGQUIT to the whole foreground
	 * process group.  The shell stays in that group (no job control), so
	 * it must not die together with the running command.
	 */
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);

	if (init_shell_cwd() != 0)
		return 1;

	if (login_shell)
		run_login_profiles();

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
