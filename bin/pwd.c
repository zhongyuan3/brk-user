#include <libcmdfw.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	char buf[1024];

	cmdfw_init(argc, argv);

	if (getcwd(buf, sizeof(buf)) == NULL)
		return cmdfw_fail_errno("getcwd failed");

	buf[sizeof(buf) - 1] = '\0';
	puts(buf);
	return CMDFW_EXIT_OK;
}
