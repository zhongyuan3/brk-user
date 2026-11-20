#include <libcmdfw.h>
#include <limits.h>
#include <unistd.h>

static const char usage[] = "Usage: sleep <seconds>";

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;
	unsigned long secs;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);
	cmdfw_require_operand_count(&ctx, 1, usage);

	if (cmdfw_parse_ulong(cmdfw_operand(&ctx, 0), &secs, 0, UINT_MAX) !=
	    0)
		return CMDFW_EXIT_FAIL;

	return sleep((unsigned int)secs);
}
