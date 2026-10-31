#include <apputil.h>
#include <stdio.h>
#include <string.h>
#include <sys/capability.h>

static const char usage[] = "Usage: cap [-d]\n"
			    "Show or drop process capabilities.\n"
			    "\n"
			    "Options:\n"
			    "  -d, --drop   drop all capabilities\n"
			    "  --help       display this help and exit";

static const struct app_option opts[] = {
	{ 'd', 'd', "drop", APP_OPT_NO_ARG },
	{ APP_OPT_HELP, 0, "help", APP_OPT_NO_ARG },
	APP_OPTION_END,
};

static const char *const names[CAP_LAST_CAP + 1] = {
	[CAP_CHOWN] = "chown",
	[CAP_DAC_OVERRIDE] = "dac_override",
	[CAP_DAC_READ_SEARCH] = "dac_read_search",
	[CAP_FOWNER] = "fowner",
	[CAP_FSETID] = "fsetid",
	[CAP_KILL] = "kill",
	[CAP_SETGID] = "setgid",
	[CAP_SETUID] = "setuid",
	[CAP_SETPCAP] = "setpcap",
	[CAP_NET_BIND_SERVICE] = "net_bind_service",
	[CAP_NET_RAW] = "net_raw",
	[CAP_SYS_CHROOT] = "sys_chroot",
	[CAP_SYS_ADMIN] = "sys_admin",
	[CAP_SYS_TIME] = "sys_time",
	[CAP_MKNOD] = "mknod",
	[CAP_SETFCAP] = "setfcap",
};

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	struct cap_header hdr = {
		.version = _LINUX_CAPABILITY_VERSION_3,
		.pid = 0,
	};
	struct cap_data data[2];
	bool drop = false;
	int opt;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	while ((opt = app_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return APP_EXIT_USAGE;
		if (opt == APP_OPT_HELP)
			app_help_exit(usage);
		if (opt == 'd')
			drop = true;
	}

	if (drop) {
		struct cap_data zero[2] = { { 0, 0, 0 }, { 0, 0, 0 } };

		if (capset(&hdr, zero) != 0)
			return app_fail_errno("capset failed");
		return APP_EXIT_OK;
	}

	if (capget(&hdr, data) != 0)
		return app_fail_errno("capget failed");

	unsigned long long effective =
		(unsigned long long)data[0].effective |
		((unsigned long long)data[1].effective << 32);
	unsigned long long permitted =
		(unsigned long long)data[0].permitted |
		((unsigned long long)data[1].permitted << 32);
	unsigned long long inheritable =
		(unsigned long long)data[0].inheritable |
		((unsigned long long)data[1].inheritable << 32);

	printf("effective:   0x%08llx\n", effective);
	printf("permitted:   0x%08llx\n", permitted);
	printf("inheritable: 0x%08llx\n", inheritable);

	for (int i = 0; i <= CAP_LAST_CAP; i++) {
		if (!(effective & (1ULL << i)))
			continue;
		printf("  %2d %s\n", i, names[i] ? names[i] : "(unknown)");
	}

	return APP_EXIT_OK;
}
