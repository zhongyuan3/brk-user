#include <libcmdfw.h>
#include <unistd.h>

static const char usage[] = "Usage: link <target> <linkname>";

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);
	cmdfw_require_operands(&ctx, 2, usage);
	if (cmdfw_operand_count(&ctx) != 2)
		cmdfw_usage(usage);

	if (link(cmdfw_operand(&ctx, 0), cmdfw_operand(&ctx, 1)) != 0)
		return cmdfw_fail_errno("link failed");

	return CMDFW_EXIT_OK;
}
