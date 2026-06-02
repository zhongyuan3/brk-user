#include <apputil.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	char buf[1024];

	app_init(argc, argv);

	if (getcwd(buf, sizeof(buf)) == NULL)
		return app_fail_errno("getcwd failed");

	buf[sizeof(buf) - 1] = '\0';
	puts(buf);
	return APP_EXIT_OK;
}
