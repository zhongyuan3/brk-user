#include <libcmdfw.h>
#include <unistd.h>

static const char usage[] = "Usage: unlink <linkname>";

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);
	cmdfw_require_operands(&ctx, 1, usage);
	if (cmdfw_operand_count(&ctx) != 1)
		cmdfw_usage(usage);

	if (unlink(cmdfw_operand(&ctx, 0)) != 0)
		return cmdfw_fail_errno("unlink failed");

	return CMDFW_EXIT_OK;
}
