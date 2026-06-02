#include <apputil.h>
#include <sys/utsname.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	struct utsname buf;

	app_init(argc, argv);

	if (uname(&buf) < 0)
		return app_fail_errno("uname failed");

	printf("sysname: %s\n", buf.sysname);
	printf("nodename: %s\n", buf.nodename);
	printf("release: %s\n", buf.release);
	printf("version: %s\n", buf.version);
	printf("machine: %s\n", buf.machine);
	printf("domainname: %s\n", buf.domainname);
	return APP_EXIT_OK;
}
