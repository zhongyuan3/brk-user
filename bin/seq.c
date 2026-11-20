#include <libcmdfw.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static const char usage[] = "Usage: seq [FIRST [INCREMENT]] LAST\n"
			    "Print numbers from FIRST to LAST.";

int main(int argc, char *argv[])
{
	long first = 1;
	long inc = 1;
	long last;
	const char *sfirst = NULL;
	const char *sinc = NULL;
	const char *slast = NULL;

	cmdfw_init(argc, argv);

	if (argc == 2) {
		slast = argv[1];
	} else if (argc == 3) {
		sfirst = argv[1];
		slast = argv[2];
	} else if (argc == 4) {
		sfirst = argv[1];
		sinc = argv[2];
		slast = argv[3];
	} else {
		cmdfw_usage(usage);
	}

	if (sfirst &&
	    cmdfw_parse_long(sfirst, &first, LONG_MIN, LONG_MAX) != 0)
		return CMDFW_EXIT_FAIL;
	if (sinc && cmdfw_parse_long(sinc, &inc, LONG_MIN, LONG_MAX) != 0)
		return CMDFW_EXIT_FAIL;
	if (cmdfw_parse_long(slast, &last, LONG_MIN, LONG_MAX) != 0)
		return CMDFW_EXIT_FAIL;

	if (inc == 0)
		return cmdfw_fail("increment must not be zero");

	if (inc > 0) {
		for (long i = first; i <= last; i += inc)
			printf("%ld\n", i);
	} else {
		for (long i = first; i >= last; i += inc)
			printf("%ld\n", i);
	}

	return CMDFW_EXIT_OK;
}
