#include <apputil.h>
#include <stdio.h>
#include <unistd.h>

static const char usage[] = "Usage: id [-ug]\n"
			    "Print real and effective user and group IDs.\n"
			    "\n"
			    "Options:\n"
			    "  -u       print only the effective user ID\n"
			    "  -g       print only the effective group ID\n"
			    "  --help   display this help and exit";

static const struct app_option opts[] = {
	{ 'u', 'u', NULL, APP_OPT_NO_ARG },
	{ 'g', 'g', NULL, APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	bool only_uid = false;
	bool only_gid = false;
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
	}

	if (only_uid && only_gid)
		app_usage(usage);

	if (only_uid) {
		printf("%u\n", (unsigned int)geteuid());
		return APP_EXIT_OK;
	}
	if (only_gid) {
		printf("%u\n", (unsigned int)getegid());
		return APP_EXIT_OK;
	}

	uid_t ruid, euid, suid;
	gid_t rgid, egid, sgid;

	if (getresuid(&ruid, &euid, &suid) != 0)
		return app_fail_errno("getresuid failed");
	if (getresgid(&rgid, &egid, &sgid) != 0)
		return app_fail_errno("getresgid failed");

	printf("uid=%u euid=%u suid=%u gid=%u egid=%u sgid=%u\n",
	       (unsigned int)ruid, (unsigned int)euid, (unsigned int)suid,
	       (unsigned int)rgid, (unsigned int)egid, (unsigned int)sgid);

	return APP_EXIT_OK;
}
