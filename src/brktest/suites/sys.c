#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static int case_ids(struct test_ctx *ctx)
{
	pid_t pid = getpid();
	pid_t ppid = getppid();

	(void)ctx;
	if (pid <= 0)
		return test_fail_msg("sys: getpid returned %d", (int)pid);
	if (ppid < 0)
		return test_fail_msg("sys: getppid returned %d", (int)ppid);
	if (getuid() == (uid_t)-1 || geteuid() == (uid_t)-1 ||
	    getgid() == (gid_t)-1 || getegid() == (gid_t)-1)
		return test_fail_msg("sys: credential getters failed");
	return 0;
}

static int case_uname(struct test_ctx *ctx)
{
	struct utsname u;

	(void)ctx;
	if (uname(&u) != 0)
		return test_fail_msg("sys: uname failed: %d", errno);
	if (!u.sysname[0] || !u.nodename[0] || !u.release[0] ||
	    !u.version[0] || !u.machine[0])
		return test_fail_msg("sys: uname returned empty fields");
	return 0;
}

static int case_rusage(struct test_ctx *ctx)
{
	struct rusage self, children;

	(void)ctx;
	if (getrusage(RUSAGE_SELF, &self) != 0)
		return test_fail_msg("sys: getrusage self failed: %d", errno);
	if (getrusage(RUSAGE_CHILDREN, &children) != 0)
		return test_fail_msg("sys: getrusage children failed: %d",
				     errno);
	if (self.ru_utime.tv_sec < 0 || self.ru_utime.tv_usec < 0 ||
	    self.ru_stime.tv_sec < 0 || self.ru_stime.tv_usec < 0)
		return test_fail_msg("sys: getrusage returned negative time");
	return 0;
}

static int case_times(struct test_ctx *ctx)
{
	struct tms t;
	clock_t ret;

	(void)ctx;
	memset(&t, 0, sizeof(t));
	ret = times(&t);
	if (ret < 0)
		return test_fail_msg("sys: times failed: %d", errno);
	if (t.tms_utime < 0 || t.tms_stime < 0 || t.tms_cutime < 0 ||
	    t.tms_cstime < 0)
		return test_fail_msg("sys: times returned negative values");
	return 0;
}

static int read_file_nonempty(const char *path)
{
	char buf[256];
	int fd = open(path, O_RDONLY);
	ssize_t n;

	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf));
	close(fd);
	return n > 0 ? 0 : -1;
}

static int case_proc(struct test_ctx *ctx)
{
	(void)ctx;
	if (read_file_nonempty("/proc/uptime") != 0)
		return test_skip("sys: /proc/uptime unavailable");
	if (read_file_nonempty("/proc/interrupts") != 0)
		return test_skip("sys: /proc/interrupts unavailable");
	if (read_file_nonempty("/proc/meminfo") != 0)
		return test_skip("sys: /proc/meminfo unavailable");
	return 0;
}

static int case_getcwd(struct test_ctx *ctx)
{
	char buf[256];

	(void)ctx;
	if (!getcwd(buf, sizeof(buf)))
		return test_fail_msg("sys: getcwd failed: %d", errno);
	if (buf[0] != '/')
		return test_fail_msg("sys: getcwd is not absolute: %s", buf);
	return 0;
}

static const struct test_case sys_cases[] = {
	{ "ids", case_ids, 0, 10000 },
	{ "uname", case_uname, 0, 10000 },
	{ "rusage", case_rusage, 0, 10000 },
	{ "times", case_times, 0, 10000 },
	{ "proc", case_proc, 0, 10000 },
	{ "getcwd", case_getcwd, 0, 10000 },
};

const struct test_suite brktest_sys_suite = {
	.name = "sys",
	.description = "process identity, uname, rusage, times and /proc",
	.cases = sys_cases,
	.nr_cases = sizeof(sys_cases) / sizeof(sys_cases[0]),
};
