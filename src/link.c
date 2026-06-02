#include <apputil.h>
#include <unistd.h>

static const char usage[] = "Usage: link <target> <linkname>";

int main(int argc, char *argv[])
{
	struct app_optctx ctx;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);
	app_require_operands(&ctx, 2, usage);
	if (app_operand_count(&ctx) != 2)
		app_usage(usage);

	if (link(app_operand(&ctx, 0), app_operand(&ctx, 1)) != 0)
		return app_fail_errno("link failed");

	return APP_EXIT_OK;
}
