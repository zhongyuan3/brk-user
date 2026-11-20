#include <libcmdfw.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);
	if (cmdfw_operand_count(&ctx) > 0)
		cmdfw_usage("Usage: sync");

	sync();
	return CMDFW_EXIT_OK;
}
