#include <libcmdfw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char help[] = "Usage: uptime\n"
			   "Show how long the system has been running.";

int main(int argc, char *argv[])
{
	char buf[128];
	FILE *f;
	char *end;
	unsigned long seconds;

	cmdfw_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		cmdfw_help_exit(help);
	if (argc != 1)
		cmdfw_usage(help);

	f = fopen("/proc/uptime", "r");
	if (!f)
		return cmdfw_fail_errno("/proc/uptime");
	if (!fgets(buf, sizeof(buf), f)) {
		fclose(f);
		return cmdfw_fail("/proc/uptime is empty");
	}
	fclose(f);

	seconds = strtoul(buf, &end, 10);
	if (end == buf)
		return cmdfw_fail("cannot parse /proc/uptime");

	unsigned long days = seconds / 86400;
	unsigned long rem = seconds % 86400;
	unsigned long hours = rem / 3600;
	unsigned long minutes = (rem % 3600) / 60;
	unsigned long secs = rem % 60;

	printf("up %lu day%s, %02lu:%02lu:%02lu\n", days, days == 1 ? "" : "s",
	       hours, minutes, secs);
	return CMDFW_EXIT_OK;
}
