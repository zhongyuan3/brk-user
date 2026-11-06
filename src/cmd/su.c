#include <brk/cmd.h>
#include <brk/limits.h>
#include <grp.h>
#include <pwd.h>
#include <pwutil.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char usage[] =
	"Usage: su [USER]\n"
	"Run a shell with the identity of USER (default root).";

static void init_groups(struct passwd *pw)
{
	gid_t groups[NGROUPS_MAX];
	int ngroups = NGROUPS_MAX;

	memset(groups, 0, sizeof(groups));
	if (getgrouplist(pw->pw_name, pw->pw_gid, groups, &ngroups) < 0)
		setgroups(0, NULL);
	else
		setgroups(ngroups, groups);
}

int main(int argc, char *argv[])
{
	const char *user = argc > 1 ? argv[1] : "root";
	struct passwd *pw;
	uid_t real = getuid();

	brk_cmd_init(argc, argv);

	if (argc > 2)
		brk_cmd_usage(usage);

	pw = getpwnam(user);
	if (!pw)
		return brk_cmd_fail("unknown user '%s'", user);

	if (real != 0) {
		struct spwd *sp = getspnam(user);
		char password[128];
		char prompt[64];

		if (!sp || !sp->sp_pwdp[0])
			return brk_cmd_fail("no password entry for '%s'", user);

		snprintf(prompt, sizeof(prompt), "Password: ");
		fputs(prompt, stdout);
		fflush(stdout);
		if (read_password(password, sizeof(password)) < 0)
			return BRK_CMD_EXIT_FAIL;
		if (pw_verify(password, sp->sp_pwdp) != 0) {
			brk_cmd_error("incorrect password");
			sleep(1);
			return BRK_CMD_EXIT_FAIL;
		}
	}

	init_groups(pw);

	if (setgid(pw->pw_gid) != 0)
		return brk_cmd_fail_errno("setgid failed");
	if (setuid(pw->pw_uid) != 0)
		return brk_cmd_fail_errno("setuid failed");

	if (pw->pw_dir && pw->pw_dir[0])
		(void)chdir(pw->pw_dir);

	const char *shell = (pw->pw_shell && pw->pw_shell[0]) ? pw->pw_shell :
								"/bin/sh";
	char *sh_argv[] = { (char *)shell, NULL };

	execv(shell, sh_argv);
	return brk_cmd_fail_errno("exec shell failed");
}
