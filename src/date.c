#include <apputil.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const char usage[] = "Usage: date [-u] [+FORMAT]\n"
			    "Print the current date and time (UTC).\n"
			    "\n"
			    "  +%s      print seconds since the epoch\n"
			    "  --help   display this help and exit";

/* Howard Hinnant's civil_from_days. */
static void civil_from_days(long z, int *year, unsigned int *month,
			    unsigned int *day)
{
	z += 719468;
	long era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned int doe = (unsigned int)(z - era * 146097);
	unsigned int yoe =
		(doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int y = (int)yoe + (int)era * 400;
	unsigned int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned int mp = (5 * doy + 2) / 153;
	unsigned int d = doy - (153 * mp + 2) / 5 + 1;
	unsigned int m = mp < 10 ? mp + 3 : mp - 9;

	*year = y + (m <= 2);
	*month = m;
	*day = d;
}

int main(int argc, char *argv[])
{
	struct timeval tv;
	bool epoch = false;
	int arg = 1;

	app_init(argc, argv);

	if (arg < argc && !strcmp(argv[arg], "--help"))
		app_help_exit(usage);
	if (arg < argc && !strcmp(argv[arg], "-u"))
		arg++;
	if (arg < argc && argv[arg][0] == '+') {
		if (!strcmp(argv[arg], "+%s"))
			epoch = true;
		else {
			app_error("unsupported format '%s'", argv[arg]);
			return APP_EXIT_FAIL;
		}
	} else if (arg < argc) {
		app_usage(usage);
	}

	if (gettimeofday(&tv, NULL) != 0)
		return app_fail_errno("gettimeofday failed");

	if (epoch) {
		printf("%ld\n", tv.tv_sec);
		return APP_EXIT_OK;
	}

	long days = tv.tv_sec / 86400;
	long rem = tv.tv_sec % 86400;

	if (rem < 0) {
		rem += 86400;
		days--;
	}

	int year;
	unsigned int month, day;
	civil_from_days(days, &year, &month, &day);

	printf("%04d-%02u-%02u %02ld:%02ld:%02ld UTC\n", year, month, day,
	       rem / 3600, (rem % 3600) / 60, rem % 60);

	return APP_EXIT_OK;
}
