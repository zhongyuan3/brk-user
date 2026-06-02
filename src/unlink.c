#include <apputil.h>
#include <unistd.h>

static const char usage[] = "Usage: unlink <linkname>";

int main(int argc, char *argv[])
{
	struct app_optctx ctx;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);
	app_require_operands(&ctx, 1, usage);
	if (app_operand_count(&ctx) != 1)
		app_usage(usage);

	if (unlink(app_operand(&ctx, 0)) != 0)
		return app_fail_errno("unlink failed");

	return APP_EXIT_OK;
}
