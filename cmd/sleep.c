#include <brk/cmd.h>
#include <limits.h>
#include <unistd.h>

static const char usage[] = "Usage: sleep <seconds>";

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	unsigned long secs;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);
	brk_cmd_require_operand_count(&ctx, 1, usage);

	if (brk_cmd_parse_ulong(brk_cmd_operand(&ctx, 0), &secs, 0, UINT_MAX) !=
	    0)
		return BRK_CMD_EXIT_FAIL;

	return sleep((unsigned int)secs);
}
