#include <apputil.h>
#include <limits.h>
#include <unistd.h>

static const char usage[] = "Usage: sleep <seconds>";

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	unsigned long secs;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);
	app_require_operand_count(&ctx, 1, usage);

	if (app_parse_ulong(app_operand(&ctx, 0), &secs, 0, UINT_MAX) != 0)
		return APP_EXIT_FAIL;

	return sleep((unsigned int)secs);
}
