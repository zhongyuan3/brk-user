#include <brk/cmd.h>
#include <unistd.h>

static const char usage[] = "Usage: link <target> <linkname>";

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);
	brk_cmd_require_operands(&ctx, 2, usage);
	if (brk_cmd_operand_count(&ctx) != 2)
		brk_cmd_usage(usage);

	if (link(brk_cmd_operand(&ctx, 0), brk_cmd_operand(&ctx, 1)) != 0)
		return brk_cmd_fail_errno("link failed");

	return BRK_CMD_EXIT_OK;
}
