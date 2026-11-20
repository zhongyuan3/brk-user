#include <libcmdfw.h>
#include <errno.h>
#include <unistd.h>

/* Home the cursor and erase the whole screen (ANSI/VT100). */
static const char clear_seq[] = "\033[H\033[2J";

static int write_clear(void)
{
	const char *p = clear_seq;
	size_t left = sizeof(clear_seq) - 1;

	while (left > 0) {
		ssize_t n = write(STDOUT_FILENO, p, left);

		if (n < 0) {
			if (errno == EINTR || errno == EBUSY)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		p += n;
		left -= (size_t)n;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct cmdfw_optctx ctx;

	cmdfw_init(argc, argv);
	cmdfw_optctx_init(&ctx, argc, argv);

	if (cmdfw_operand_count(&ctx) != 0)
		cmdfw_usage("Usage: clear");

	if (cmdfw_term_check() != 0)
		return CMDFW_EXIT_FAIL;

	if (write_clear())
		return cmdfw_fail_errno("clear: write error");

	return CMDFW_EXIT_OK;
}
