#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define ENV_BASIC "BRKTEST_ENV_BASIC"
#define ENV_CLEAR "BRKTEST_ENV_CLEAR"
#define ENV_PUT "BRKTEST_ENV_PUT"

static int wait_exit(pid_t pid, int expected, const char *what)
{
	int status;

	if (waitpid(pid, &status, 0) != pid)
		return test_fail_msg("env: waitpid failed for %s", what);
	if (!WIFEXITED(status))
		return test_fail_msg("env: %s did not exit normally", what);
	if (WEXITSTATUS(status) != expected)
		return test_fail_msg("env: %s exited with %d, expected %d",
				     what, WEXITSTATUS(status), expected);
	return 0;
}

static int run_program(const char *path, char *const argv[], char *const envp[],
		       int expected)
{
	pid_t pid = fork();

	if (pid < 0)
		return test_fail_msg("env: fork failed: %d", errno);
	if (pid == 0) {
		int null = open("/dev/null", O_WRONLY);

		if (null >= 0) {
			(void)dup2(null, STDOUT_FILENO);
			if (null > STDOUT_FILENO)
				close(null);
		}
		execve(path, argv, envp);
		_exit(127);
	}

	return wait_exit(pid, expected, path);
}

static int case_term(struct test_ctx *ctx)
{
	char *empty_env[] = { "PATH=/bin", NULL };
	char *vt100_env[] = { "PATH=/bin", "TERM=vt100", NULL };
	char *dumb_env[] = { "PATH=/bin", "TERM=dumb", NULL };
	char *clear_argv[] = { "clear", NULL };
	char *tput_argv[] = { "tput", "clear", NULL };
	char *version_argv[] = { "tput", "-V", NULL };

	(void)ctx;

	if (run_program("/usr/bin/clear", clear_argv, empty_env, 1) != 0)
		return -1;
	if (run_program("/usr/bin/clear", clear_argv, dumb_env, 1) != 0)
		return -1;
	if (run_program("/usr/bin/clear", clear_argv, vt100_env, 0) != 0)
		return -1;
	if (run_program("/usr/bin/tput", tput_argv, empty_env, 1) != 0)
		return -1;
	if (run_program("/usr/bin/tput", tput_argv, vt100_env, 0) != 0)
		return -1;
	if (run_program("/usr/bin/tput", version_argv, empty_env, 0) != 0)
		return -1;
	return 0;
}

static int case_basic(struct test_ctx *ctx)
{
	const char *value;

	(void)ctx;

	(void)unsetenv(ENV_BASIC);

	if (setenv(ENV_BASIC, "one", 1) != 0)
		return test_fail_msg("env: setenv failed: %d", errno);

	value = getenv(ENV_BASIC);
	if (!value || strcmp(value, "one"))
		return test_fail_msg(
			"env: getenv did not return the new value");

	if (setenv(ENV_BASIC, "two", 0) != 0)
		return test_fail_msg("env: setenv overwrite=0 failed");
	value = getenv(ENV_BASIC);
	if (!value || strcmp(value, "one"))
		return test_fail_msg("env: overwrite=0 replaced the value");

	if (setenv(ENV_BASIC, "two", 1) != 0)
		return test_fail_msg("env: setenv overwrite=1 failed");
	value = getenv(ENV_BASIC);
	if (!value || strcmp(value, "two"))
		return test_fail_msg(
			"env: overwrite=1 did not replace the value");

	if (setenv(ENV_BASIC, "", 1) != 0)
		return test_fail_msg("env: setenv with an empty value failed");
	value = getenv(ENV_BASIC);
	if (!value || value[0])
		return test_fail_msg("env: empty value was not preserved");

	errno = 0;
	if (setenv("BRKTEST=BAD", "x", 1) != -1 || errno != EINVAL)
		return test_fail_msg("env: invalid setenv name was accepted");

	errno = 0;
	if (unsetenv("BRKTEST=BAD") != -1 || errno != EINVAL)
		return test_fail_msg("env: invalid unsetenv name was accepted");

	if (unsetenv(ENV_BASIC) != 0)
		return test_fail_msg("env: unsetenv failed: %d", errno);
	if (getenv(ENV_BASIC) != NULL)
		return test_fail_msg("env: unsetenv left the variable visible");

	return 0;
}

static int case_clearenv_putenv(struct test_ctx *ctx)
{
	const char *old_path = getenv("PATH");
	char *saved_path = old_path ? strdup(old_path) : NULL;
	char put[] = ENV_PUT "=hello";
	char bad[] = "BRKTEST_ENV_BAD";
	char bad_name[] = "=value";
	const char *value;

	(void)ctx;

	if (clearenv() != 0)
		return test_fail_msg("env: clearenv failed: %d", errno);
	if (environ && environ[0])
		return test_fail_msg("env: clearenv left entries behind");

	if (setenv(ENV_CLEAR, "1", 1) != 0)
		return test_fail_msg("env: setenv after clearenv failed: %d",
				     errno);
	if (!getenv(ENV_CLEAR))
		return test_fail_msg(
			"env: setenv after clearenv is not visible");

	if (putenv(put) != 0)
		return test_fail_msg("env: putenv failed: %d", errno);
	value = getenv(ENV_PUT);
	if (!value || strcmp(value, "hello"))
		return test_fail_msg("env: putenv value mismatch");

	errno = 0;
	if (putenv(bad) != -1 || errno != EINVAL)
		return test_fail_msg("env: putenv accepted a name without '='");
	errno = 0;
	if (putenv(bad_name) != -1 || errno != EINVAL)
		return test_fail_msg("env: putenv accepted an empty name");

	/*
	 * environ is a public global.  Replacing it must not corrupt the
	 * library's ownership state for later setenv() calls.
	 */
	environ = NULL;
	if (setenv(ENV_CLEAR, "2", 1) != 0)
		return test_fail_msg(
			"env: setenv after environ=NULL failed: %d", errno);
	value = getenv(ENV_CLEAR);
	if (!value || strcmp(value, "2"))
		return test_fail_msg(
			"env: setenv after environ=NULL lost data");

	(void)unsetenv(ENV_CLEAR);
	(void)unsetenv(ENV_PUT);

	if (saved_path) {
		if (setenv("PATH", saved_path, 1) != 0) {
			free(saved_path);
			return test_fail_msg("env: cannot restore PATH");
		}
		free(saved_path);
	}

	return 0;
}

static int case_exec_env(struct test_ctx *ctx)
{
	pid_t pid;
	int status;

	(void)ctx;
	pid = fork();
	if (pid < 0)
		return test_fail_msg("env: fork failed: %d", errno);
	if (pid == 0) {
		char *argv[] = { "sh", "-c", "exit $BRKTEST_EXEC_ENV", NULL };
		char *envp[] = { "BRKTEST_EXEC_ENV=7", NULL };

		execve("/bin/sh", argv, envp);
		_exit(127);
	}

	if (waitpid(pid, &status, 0) != pid)
		return test_fail_msg("env: waitpid failed: %d", errno);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 7)
		return test_fail_msg("env: custom execve environment not used");
	return 0;
}

static int case_exec_variants(struct test_ctx *ctx)
{
	pid_t pid;

	(void)ctx;

	pid = fork();
	if (pid < 0)
		return test_fail_msg("env: fork failed: %d", errno);
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", "exit 3", (char *)NULL);
		_exit(127);
	}
	if (wait_exit(pid, 3, "execl") != 0)
		return -1;

	pid = fork();
	if (pid < 0)
		return test_fail_msg("env: fork failed: %d", errno);
	if (pid == 0) {
		char *envp[] = { "BRKTEST_EXEC_ENV=5", NULL };

		execle("/bin/sh", "sh", "-c", "exit $BRKTEST_EXEC_ENV",
		       (char *)NULL, envp);
		_exit(127);
	}
	if (wait_exit(pid, 5, "execle") != 0)
		return -1;

	if (setenv("PATH", "/bin", 1) != 0)
		return test_fail_msg("env: cannot set PATH for execlp");

	pid = fork();
	if (pid < 0)
		return test_fail_msg("env: fork failed: %d", errno);
	if (pid == 0) {
		execlp("true", "true", (char *)NULL);
		_exit(127);
	}
	if (wait_exit(pid, 0, "execlp") != 0)
		return -1;

	return 0;
}

static int case_execvpe_path(struct test_ctx *ctx)
{
	char dir[256];
	char cwd[256];
	char target[300];
	char *saved_path = NULL;
	const char *old_path = getenv("PATH");
	pid_t pid;
	int status;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "envpath") != 0)
		return test_fail_msg("env: cannot create temp directory");
	if (snprintf(target, sizeof(target), "%s/true", dir) >=
	    (int)sizeof(target))
		return test_fail_msg("env: temp path too long");
	if (symlink("/bin/false", target) != 0)
		return test_fail_msg("env: cannot create CWD decoy: %d", errno);
	if (!getcwd(cwd, sizeof(cwd) - 1))
		return test_fail_msg("env: getcwd failed: %d", errno);
	cwd[sizeof(cwd) - 1] = '\0';
	if (chdir(dir) != 0)
		return test_fail_msg("env: chdir failed: %d", errno);

	if (old_path)
		saved_path = strdup(old_path);
	if (setenv("PATH", "/bin", 1) != 0)
		return test_fail_msg("env: cannot set PATH");

	pid = fork();
	if (pid < 0)
		return test_fail_msg("env: fork failed: %d", errno);
	if (pid == 0) {
		char *argv[] = { "true", NULL };

		execvpe("true", argv, environ);
		_exit(127);
	}

	if (waitpid(pid, &status, 0) != pid)
		return test_fail_msg("env: waitpid failed: %d", errno);

	(void)chdir(cwd);
	if (saved_path)
		(void)setenv("PATH", saved_path, 1);
	else
		(void)unsetenv("PATH");
	free(saved_path);
	(void)unlink(target);
	(void)rmdir(dir);

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return test_fail_msg("env: execvpe executed the CWD decoy");
	return 0;
}

static int write_script(const char *path, const char *content)
{
	FILE *f = fopen(path, "w");
	int failed;

	if (!f)
		return -1;
	failed = fputs(content, f) == EOF;
	if (fclose(f) != 0)
		failed = 1;
	return failed ? -1 : 0;
}

static int run_shell_script(const char *script, char *const envp[],
			    const char *cwd, int expected)
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return test_fail_msg("env: fork failed: %d", errno);
	if (pid == 0) {
		char *argv[] = { "sh", (char *)script, NULL };

		if (cwd && chdir(cwd) != 0)
			_exit(127);
		execve("/bin/sh", argv, envp);
		_exit(127);
	}

	if (waitpid(pid, &status, 0) != pid)
		return test_fail_msg("env: waitpid failed: %d", errno);
	if (!WIFEXITED(status))
		return test_fail_msg("env: shell did not exit normally");
	if (WEXITSTATUS(status) != expected)
		return test_fail_msg("env: shell exited with %d, expected %d",
				     WEXITSTATUS(status), expected);
	return 0;
}

static int case_export_unset(struct test_ctx *ctx)
{
	char dir[256];
	char script[300];
	char *empty_env[] = { NULL };
	char *path_env[] = { "PATH=/bin", NULL };
	char *var_env[] = { "PATH=/bin", "BRKTEST_UNSET=1", NULL };

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "envshell") != 0)
		return test_fail_msg("env: cannot create temp directory");
	if (snprintf(script, sizeof(script), "%s/script", dir) >=
	    (int)sizeof(script))
		return test_fail_msg("env: script path too long");

	if (write_script(script, "export PATH\ntrue\n") != 0)
		return test_fail_msg("env: cannot write export script");
	if (run_shell_script(script, empty_env, "/", 0) != 0)
		return -1;

	if (write_script(script, "BRKTEST_ASSIGN=7\n"
				 "export BRKTEST_ASSIGN\n"
				 "exit $BRKTEST_ASSIGN\n") != 0)
		return test_fail_msg("env: cannot write assignment script");
	if (run_shell_script(script, empty_env, "/", 7) != 0)
		return -1;

	if (write_script(script, "BRKTEST_ASSIGN=7 /bin/sh -c "
				 "'exit $BRKTEST_ASSIGN'\n") != 0)
		return test_fail_msg("env: cannot write prefix script");
	if (run_shell_script(script, path_env, "/", 7) != 0)
		return -1;

	if (write_script(script, "unset BRKTEST_UNSET\n"
				 "/bin/test -z \"$BRKTEST_UNSET\"\n") != 0)
		return test_fail_msg("env: cannot write unset script");
	if (run_shell_script(script, var_env, "/", 0) != 0)
		return -1;

	(void)unlink(script);
	(void)rmdir(dir);
	return 0;
}

static int case_locale(struct test_ctx *ctx)
{
	const char *old_lang = getenv("LANG");
	const char *old_all = getenv("LC_ALL");
	char *saved_lang = old_lang ? strdup(old_lang) : NULL;
	char *saved_all = old_all ? strdup(old_all) : NULL;
	struct lconv *lc;

	(void)ctx;

	if (!setlocale(LC_ALL, NULL) || strcmp(setlocale(LC_ALL, NULL), "C"))
		return test_fail_msg("env: initial locale is not C");
	if (!setlocale(LC_ALL, "C") || !setlocale(LC_ALL, "POSIX"))
		return test_fail_msg("env: cannot select the C locale");
	if (setlocale(LC_ALL, "en_US.UTF-8") != NULL)
		return test_fail_msg("env: unsupported locale was accepted");

	(void)unsetenv("LC_ALL");
	if (setenv("LANG", "C", 1) != 0)
		return test_fail_msg("env: cannot set LANG");
	if (!setlocale(LC_ALL, ""))
		return test_fail_msg("env: LANG=C was not selected");

	if (setenv("LANG", "en_US.UTF-8", 1) != 0)
		return test_fail_msg("env: cannot set LANG");
	if (setlocale(LC_ALL, "") != NULL)
		return test_fail_msg("env: unsupported LANG was selected");

	(void)unsetenv("LANG");
	if (setenv("LC_CTYPE", "C", 1) != 0 ||
	    setenv("LC_TIME", "en_US.UTF-8", 1) != 0)
		return test_fail_msg("env: cannot set locale variables");
	if (!setlocale(LC_CTYPE, ""))
		return test_fail_msg("env: LC_CTYPE=C was not selected");
	if (setlocale(LC_TIME, "") != NULL)
		return test_fail_msg("env: unsupported LC_TIME was selected");

	lc = localeconv();
	if (!lc || strcmp(lc->decimal_point, ".") || lc->thousands_sep[0])
		return test_fail_msg("env: C locale numeric format is wrong");

	(void)unsetenv("LC_CTYPE");
	(void)unsetenv("LC_TIME");
	if (saved_lang)
		(void)setenv("LANG", saved_lang, 1);
	else
		(void)unsetenv("LANG");
	if (saved_all)
		(void)setenv("LC_ALL", saved_all, 1);
	else
		(void)unsetenv("LC_ALL");
	free(saved_lang);
	free(saved_all);
	return 0;
}

static int case_cd_home(struct test_ctx *ctx)
{
	char dir[256];
	char home[300];
	char script[300];
	char output[300];
	char line[512];
	char *envp[] = { home, "PATH=/bin", NULL };
	FILE *f;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "envhome") != 0)
		return test_fail_msg("env: cannot create home directory");
	if (snprintf(home, sizeof(home), "HOME=%s", dir) >= (int)sizeof(home))
		return test_fail_msg("env: home path too long");
	if (snprintf(script, sizeof(script), "%s/script", dir) >=
		    (int)sizeof(script) ||
	    snprintf(output, sizeof(output), "%s/output", dir) >=
		    (int)sizeof(output))
		return test_fail_msg("env: temp path too long");

	/* The output redirection path is generated at runtime. */
	f = fopen(script, "w");
	if (!f)
		return test_fail_msg("env: cannot open cd script");
	if (fprintf(f,
		    "cd\n"
		    "/bin/pwd > %s\n"
		    "/bin/printenv PWD >> %s\n"
		    "/bin/printenv OLDPWD >> %s\n",
		    output, output, output) < 0 ||
	    fclose(f) != 0)
		return test_fail_msg("env: cannot write cd script");

	if (run_shell_script(script, envp, "/", 0) != 0)
		return -1;

	f = fopen(output, "r");
	if (!f)
		return test_fail_msg("env: cannot read cd output");

	const char *expected[] = { dir, dir, "/" };

	for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
		size_t n;

		if (!fgets(line, sizeof(line), f)) {
			fclose(f);
			return test_fail_msg("env: incomplete cd output");
		}
		n = strlen(expected[i]);
		if (strlen(line) != n + 1 || strncmp(line, expected[i], n) ||
		    line[n] != '\n') {
			fclose(f);
			return test_fail_msg(
				"env: cd state mismatch (got '%s')", line);
		}
	}
	fclose(f);

	(void)unlink(script);
	(void)unlink(output);
	(void)rmdir(dir);
	return 0;
}

static int case_profile(struct test_ctx *ctx)
{
	char dir[256];
	char home[300];
	char profile[300];
	char *envp[] = { home, "PATH=/bin", NULL };
	pid_t pid;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "envprofile") != 0)
		return test_fail_msg("env: cannot create profile directory");
	if (snprintf(home, sizeof(home), "HOME=%s", dir) >= (int)sizeof(home))
		return test_fail_msg("env: profile home path too long");
	if (snprintf(profile, sizeof(profile), "%s/.profile", dir) >=
	    (int)sizeof(profile))
		return test_fail_msg("env: profile path too long");
	if (write_script(profile, "BRKTEST_PROFILE=7\n"
				  "export BRKTEST_PROFILE\n") != 0)
		return test_fail_msg("env: cannot write .profile");

	pid = fork();
	if (pid < 0)
		return test_fail_msg("env: fork failed: %d", errno);
	if (pid == 0) {
		char *argv[] = { "-sh", "-c", "exit $BRKTEST_PROFILE", NULL };

		if (chdir("/") != 0)
			_exit(127);
		execve("/bin/sh", argv, envp);
		_exit(127);
	}

	if (wait_exit(pid, 7, "login profile") != 0)
		return -1;

	(void)unlink(profile);
	(void)rmdir(dir);
	return 0;
}

static int case_prompt(struct test_ctx *ctx)
{
	char dir[256];
	char output[300];
	char line[128];
	char expected[64];
	char *envp[] = { "PS1=BRK:\\u:\\w:\\$ ", "HOME=/", "USER=tester",
			 "PATH=/bin", NULL };
	pid_t pid;
	int fd;

	(void)ctx;
	if (test_tmpdir(dir, sizeof(dir), "envprompt") != 0)
		return test_fail_msg("env: cannot create prompt directory");
	if (snprintf(output, sizeof(output), "%s/output", dir) >=
	    (int)sizeof(output))
		return test_fail_msg("env: prompt path too long");

	fd = open(output, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0)
		return test_fail_msg("env: cannot create prompt output: %d",
				     errno);

	pid = fork();
	if (pid < 0)
		return test_fail_msg("env: fork failed: %d", errno);
	if (pid == 0) {
		char *argv[] = { "sh", NULL };
		int p[2];

		if (pipe(p) != 0)
			_exit(127);
		close(p[1]);
		if (dup2(p[0], STDIN_FILENO) < 0)
			_exit(127);
		close(p[0]);
		(void)dup2(fd, STDOUT_FILENO);
		close(fd);
		if (chdir("/") != 0)
			_exit(127);
		execve("/bin/sh", argv, envp);
		_exit(127);
	}

	close(fd);
	if (wait_exit(pid, 0, "prompt") != 0)
		return -1;

	fd = open(output, O_RDONLY);
	if (fd < 0)
		return test_fail_msg("env: cannot read prompt output");
	ssize_t n = read(fd, line, sizeof(line) - 1);
	close(fd);
	if (n < 0)
		return test_fail_msg("env: cannot read prompt output");
	line[n] = '\0';

	if (snprintf(expected, sizeof(expected), "BRK:tester:~:%c ",
		     geteuid() == 0 ? '#' : '$') >= (int)sizeof(expected))
		return test_fail_msg("env: expected prompt too long");
	if (strcmp(line, expected))
		return test_fail_msg("env: PS1 mismatch (got '%s', want '%s')",
				     line, expected);

	(void)unlink(output);
	(void)rmdir(dir);
	return 0;
}

static const struct test_case env_cases[] = {
	{ "basic", case_basic, 0, 10000 },
	{ "clearenv", case_clearenv_putenv, 0, 10000 },
	{ "exec", case_exec_env, 0, 10000 },
	{ "execl", case_exec_variants, 0, 10000 },
	{ "execvpe", case_execvpe_path, 0, 10000 },
	{ "shell", case_export_unset, 0, 10000 },
	{ "home", case_cd_home, 0, 10000 },
	{ "term", case_term, 0, 10000 },
	{ "locale", case_locale, 0, 10000 },
	{ "profile", case_profile, 0, 10000 },
	{ "prompt", case_prompt, 0, 10000 },
};

const struct test_suite brktest_env_suite = {
	.name = "env",
	.description = "environment variables, execve and shell integration",
	.cases = env_cases,
	.nr_cases = sizeof(env_cases) / sizeof(env_cases[0]),
};
