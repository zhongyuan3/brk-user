#include <brk/cmd.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);
	if (brk_cmd_operand_count(&ctx) > 0)
		brk_cmd_usage("Usage: sync");

	sync();
	return BRK_CMD_EXIT_OK;
}
