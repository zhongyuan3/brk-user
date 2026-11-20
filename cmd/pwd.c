#include <brk/cmd.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	char buf[1024];

	brk_cmd_init(argc, argv);

	if (getcwd(buf, sizeof(buf)) == NULL)
		return brk_cmd_fail_errno("getcwd failed");

	buf[sizeof(buf) - 1] = '\0';
	puts(buf);
	return BRK_CMD_EXIT_OK;
}
