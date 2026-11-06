#include <brk/cmd.h>
#include <brk/limits.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: groups [USER...]\n"
			    "Print the groups a user is in.\n"
			    "\n"
			    "Options:\n"
			    "  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static void print_group(gid_t gid)
{
	struct group *gr = getgrgid(gid);

	if (gr)
		fputs(gr->gr_name, stdout);
	else
		printf("%u", (unsigned int)gid);
}

static int print_current_groups(void)
{
	gid_t groups[NGROUPS_MAX];
	int n = getgroups(NGROUPS_MAX, groups);
	gid_t egid = getegid();
	bool first = true;

	print_group(egid);
	first = false;

	if (n < 0)
		return -1;
	for (int i = 0; i < n; i++) {
		if (groups[i] == egid)
			continue;
		if (!first)
			putchar(' ');
		print_group(groups[i]);
		first = false;
	}
	putchar('\n');
	return 0;
}

static int print_user_groups(const char *user)
{
	gid_t groups[NGROUPS_MAX];
	struct passwd *pw = getpwnam(user);
	int n = NGROUPS_MAX;

	if (!pw)
		return -1;
	if (getgrouplist(user, pw->pw_gid, groups, &n) < 0) {
		errno = EINVAL;
		return -1;
	}

	printf("%s :", user);
	for (int i = 0; i < n; i++) {
		putchar(' ');
		print_group(groups[i]);
	}
	putchar('\n');
	return 0;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
	}

	if (brk_cmd_operand_count(&ctx) == 0) {
		if (print_current_groups() != 0)
			return brk_cmd_fail_errno("groups");
		return BRK_CMD_EXIT_OK;
	}

	for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
		const char *user = brk_cmd_operand(&ctx, i);

		if (print_user_groups(user) != 0) {
			brk_cmd_error("%s: %s", user, strerror(errno));
			ret = BRK_CMD_EXIT_FAIL;
		}
	}
	return ret;
}
