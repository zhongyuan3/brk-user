#include <apputil.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	struct app_optctx ctx;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);
	if (app_operand_count(&ctx) > 0)
		app_usage("Usage: sync");

	sync();
	return APP_EXIT_OK;
}
