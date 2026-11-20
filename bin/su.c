#include <libcmdfw.h>
#include <errno.h>
#include <pwd.h>
#include <brk/pwutil.h>
#include <shadow.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char usage[] =
	"Usage: su [-l|-] [USER]\n"
	"Run a shell with the identity of USER (default root).\n"
	"\n"
	"Options:\n"
	"  -, -l, --login  start a login shell with a clean environment";

static int change_to_home(const struct passwd *pw)
{
	if (!pw->pw_dir || !pw->pw_dir[0])
		return 0;
	if (chdir(pw->pw_dir) != 0) {
		cmdfw_warn("cannot change directory to %s: %s", pw->pw_dir,
			     strerror(errno));
		return -1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	bool login = false;
	int argi = 1;
	const char *user;
	struct passwd *pw;
	uid_t real = getuid();
	char term[64];
	const char *old_term;

	cmdfw_init(argc, argv);

	if (argi < argc) {
		if (!strcmp(argv[argi], "-") || !strcmp(argv[argi], "-l") ||
		    !strcmp(argv[argi], "--login")) {
			login = true;
			argi++;
		}
	}

	if (argc > argi + 1)
		cmdfw_usage(usage);
	user = argi < argc ? argv[argi] : "root";

	pw = getpwnam(user);
	if (!pw)
		return cmdfw_fail("unknown user '%s'", user);

	if (real != 0) {
		struct spwd *sp = getspnam(user);
		char password[128];

		if (!sp || !sp->sp_pwdp[0])
			return cmdfw_fail("no password entry for '%s'", user);

		fputs("Password: ", stdout);
		fflush(stdout);
		if (read_password(password, sizeof(password)) < 0)
			return CMDFW_EXIT_FAIL;
		if (pw_verify(password, sp->sp_pwdp) != 0) {
			cmdfw_error("incorrect password");
			sleep(1);
			return CMDFW_EXIT_FAIL;
		}
	}

	if (setgid(pw->pw_gid) != 0)
		return cmdfw_fail_errno("setgid failed");
	if (setuid(pw->pw_uid) != 0)
		return cmdfw_fail_errno("setuid failed");

	if (login)
		(void)change_to_home(pw);

	term[0] = '\0';
	old_term = getenv("TERM");
	if (old_term) {
		strncpy(term, old_term, sizeof(term) - 1);
		term[sizeof(term) - 1] = '\0';
	}

	if (login) {
		if (cmdfw_env_login(pw, term[0] ? term : NULL) != 0)
			return cmdfw_fail("cannot set login environment");
	} else if (cmdfw_env_user(pw) != 0) {
		return cmdfw_fail("cannot set user environment");
	}

	const char *shell = (pw->pw_shell && pw->pw_shell[0]) ? pw->pw_shell :
								"/bin/sh";
	char sh_name[256];
	const char *argv0 = shell;

	if (login) {
		const char *base = strrchr(shell, '/');

		base = base ? base + 1 : shell;
		int n = snprintf(sh_name, sizeof(sh_name), "-%s", base);

		if (n > 0 && (size_t)n < sizeof(sh_name))
			argv0 = sh_name;
	}

	char *sh_argv[] = { (char *)argv0, NULL };

	execv(shell, sh_argv);
	return cmdfw_fail_errno("exec shell failed");
}
