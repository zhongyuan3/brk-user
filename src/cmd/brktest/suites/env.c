#include "../harness.h"
#include "../util.h"

#include <errno.h>
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

static const struct test_case env_cases[] = {
	{ "basic", case_basic, 0, 10000 },
	{ "clearenv", case_clearenv_putenv, 0, 10000 },
	{ "exec", case_exec_env, 0, 10000 },
	{ "execl", case_exec_variants, 0, 10000 },
	{ "execvpe", case_execvpe_path, 0, 10000 },
	{ "shell", case_export_unset, 0, 10000 },
	{ "home", case_cd_home, 0, 10000 },
};

const struct test_suite brktest_env_suite = {
	.name = "env",
	.description = "environment variables, execve and shell integration",
	.cases = env_cases,
	.nr_cases = sizeof(env_cases) / sizeof(env_cases[0]),
};
