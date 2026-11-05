#include <brk/cmd.h>
#include <brk/limits.h>
#include <grp.h>
#include <pwd.h>
#include <pwutil.h>
#include <shadow.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: login [USER]\n"
			    "Log in as USER and start a shell.";

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

static int login_as(const char *user)
{
	struct passwd *pw = getpwnam(user);
	struct spwd *sp = getspnam(user);
	char password[128];

	if (!pw || !sp)
		return -1;

	fputs("Password: ", stdout);
	fflush(stdout);
	if (read_password(password, sizeof(password)) < 0)
		return -1;
	if (pw_verify(password, sp->sp_pwdp) != 0)
		return -1;

	init_groups(pw);
	if (setgid(pw->pw_gid) != 0 || setuid(pw->pw_uid) != 0)
		return -1;

	if (pw->pw_dir && pw->pw_dir[0])
		(void)chdir(pw->pw_dir);

	const char *shell = (pw->pw_shell && pw->pw_shell[0]) ? pw->pw_shell :
								"/bin/sh";
	char *sh_argv[] = { (char *)shell, NULL };

	execv(shell, sh_argv);
	return -1;
}

int main(int argc, char *argv[])
{
	char user[64];

	brk_cmd_init(argc, argv);

	if (argc > 2)
		brk_cmd_usage(usage);

	if (argc > 1) {
		strncpy(user, argv[1], sizeof(user) - 1);
		user[sizeof(user) - 1] = '\0';
	} else {
		fputs("login: ", stdout);
		fflush(stdout);
		if (!fgets(user, sizeof(user), stdin))
			return BRK_CMD_EXIT_FAIL;

		size_t len = strlen(user);

		while (len && (user[len - 1] == '\n' || user[len - 1] == '\r'))
			user[--len] = '\0';
	}

	if (!user[0] || login_as(user) != 0) {
		brk_cmd_error("login incorrect");
		sleep(1);
		return BRK_CMD_EXIT_FAIL;
	}

	return BRK_CMD_EXIT_OK;
}
