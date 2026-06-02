#include <apputil.h>
#include <unistd.h>

static const char usage[] = "Usage: rm FILE...";

int main(int argc, char *argv[])
{
	struct app_optctx ctx;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);
	app_require_operands(&ctx, 1, usage);

	for (int i = 0; i < app_operand_count(&ctx); i++) {
		if (unlink(app_operand(&ctx, i)) != 0)
			return app_fail_errno("remove failed");
	}

	return APP_EXIT_OK;
}
