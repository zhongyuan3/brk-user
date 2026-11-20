#include <brk/cmd.h>
#include <brk/limits.h>

#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>

#define BRK_CMD_DEFAULT_PATH "/bin:/sbin:/usr/bin:/usr/sbin"
#define BRK_CMD_DEFAULT_TERM "vt100"

static const char *value_or(const char *value, const char *fallback)
{
	return value && value[0] ? value : fallback;
}

static int set_pwd(void)
{
	char buf[PATH_MAX];

	if (!getcwd(buf, sizeof(buf) - 1))
		return 0;
	buf[sizeof(buf) - 1] = '\0';
	return setenv("PWD", buf, 1);
}

static int set_identity(const char *user, const char *home, const char *shell)
{
	if (setenv("HOME", home, 1) != 0)
		return -1;
	if (setenv("USER", user, 1) != 0)
		return -1;
	if (setenv("LOGNAME", user, 1) != 0)
		return -1;
	if (setenv("SHELL", shell, 1) != 0)
		return -1;
	if (setenv("PATH", BRK_CMD_DEFAULT_PATH, 1) != 0)
		return -1;
	return 0;
}

/*
 * Build a fresh login environment.  Used by login(1) and su -.
 */
int brk_cmd_env_login(const struct passwd *pw, const char *term)
{
	const char *user = value_or(pw->pw_name, "unknown");
	const char *home = value_or(pw->pw_dir, "/");
	const char *shell = value_or(pw->pw_shell, "/bin/sh");

	if (clearenv() != 0)
		return -1;

	if (set_identity(user, home, shell) != 0)
		return -1;
	if (setenv("TERM", value_or(term, BRK_CMD_DEFAULT_TERM), 1) != 0)
		return -1;
	return set_pwd();
}

/*
 * Rebuild the identity-related variables when changing user without a login
 * shell.  Unknown variables are preserved, but PATH, HOME and the shell
 * identity must never be inherited from the old user.
 */
int brk_cmd_env_user(const struct passwd *pw)
{
	const char *user = value_or(pw->pw_name, "unknown");
	const char *home = value_or(pw->pw_dir, "/");
	const char *shell = value_or(pw->pw_shell, "/bin/sh");
	const char *term;

	/*
	 * These variables can alter how a shell parses or locates commands,
	 * so they must not survive a user identity change.
	 */
	(void)unsetenv("IFS");
	(void)unsetenv("ENV");
	(void)unsetenv("CDPATH");
	(void)unsetenv("BASH_ENV");
	(void)unsetenv("SHELLOPTS");

	if (set_identity(user, home, shell) != 0)
		return -1;

	term = getenv("TERM");
	if (!term || !term[0]) {
		if (setenv("TERM", BRK_CMD_DEFAULT_TERM, 1) != 0)
			return -1;
	}
	return set_pwd();
}
