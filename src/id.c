#include <apputil.h>
#include <brk/limits.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <unistd.h>

static const char usage[] =
	"Usage: id [-ugG]\n"
	"Print real and effective user and group IDs.\n"
	"\n"
	"Options:\n"
	"  -u       print only the effective user ID\n"
	"  -g       print only the effective group ID\n"
	"  -G       print the effective and supplementary group IDs\n"
	"  --help   display this help and exit";

static const struct app_option opts[] = {
	{ 'u', 'u', NULL, APP_OPT_NO_ARG },
	{ 'g', 'g', NULL, APP_OPT_NO_ARG },
	{ 'G', 'G', NULL, APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static void print_groups(gid_t egid)
{
	gid_t groups[NGROUPS_MAX];
	int n = getgroups(NGROUPS_MAX, groups);

	printf("%u", (unsigned int)egid);
	if (n < 0)
		return;

	for (int i = 0; i < n; i++) {
		if (groups[i] == egid)
			continue;
		printf(" %u", (unsigned int)groups[i]);
	}
}

static void print_user(uid_t uid)
{
	struct passwd *pw = getpwuid(uid);

	printf("%u", (unsigned int)uid);
	if (pw)
		printf("(%s)", pw->pw_name);
}

static void print_group(gid_t gid)
{
	struct group *gr = getgrgid(gid);

	printf("%u", (unsigned int)gid);
	if (gr)
		printf("(%s)", gr->gr_name);
}

static void print_groups_named(gid_t egid)
{
	gid_t groups[NGROUPS_MAX];
	int n = getgroups(NGROUPS_MAX, groups);

	print_group(egid);
	if (n < 0)
		return;

	for (int i = 0; i < n; i++) {
		if (groups[i] == egid)
			continue;
		putchar(' ');
		print_group(groups[i]);
	}
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	bool only_uid = false;
	bool only_gid = false;
	bool only_groups = false;
	int opt;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
		if (opt == 'u')
			only_uid = true;
		if (opt == 'g')
			only_gid = true;
		if (opt == 'G')
			only_groups = true;
	}

	if (only_uid + only_gid + only_groups > 1)
		app_usage(usage);

	if (only_uid) {
		printf("%u\n", (unsigned int)geteuid());
		return APP_EXIT_OK;
	}
	if (only_gid) {
		printf("%u\n", (unsigned int)getegid());
		return APP_EXIT_OK;
	}
	if (only_groups) {
		print_groups(getegid());
		putchar('\n');
		return APP_EXIT_OK;
	}

	uid_t ruid, euid, suid;
	gid_t rgid, egid, sgid;

	if (getresuid(&ruid, &euid, &suid) != 0)
		return app_fail_errno("getresuid failed");
	if (getresgid(&rgid, &egid, &sgid) != 0)
		return app_fail_errno("getresgid failed");

	fputs("uid=", stdout);
	print_user(ruid);
	fputs(" euid=", stdout);
	print_user(euid);
	fputs(" suid=", stdout);
	print_user(suid);
	fputs(" gid=", stdout);
	print_group(rgid);
	fputs(" egid=", stdout);
	print_group(egid);
	fputs(" sgid=", stdout);
	print_group(sgid);
	putchar('\n');

	fputs("groups=", stdout);
	print_groups_named(egid);
	putchar('\n');

	return APP_EXIT_OK;
}
