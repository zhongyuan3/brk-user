#include <apputil.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] = "Usage: mkdir DIR...";

int main(int argc, char *argv[])
{
	struct app_optctx ctx;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);
	app_require_operands(&ctx, 1, usage);

	for (int i = 0; i < app_operand_count(&ctx); i++) {
		if (mkdir(app_operand(&ctx, i), 0) != 0)
			return app_fail_errno("mkdir failed");
	}

	return APP_EXIT_OK;
}
