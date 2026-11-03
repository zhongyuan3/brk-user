#include <apputil.h>
#include <brk/limits.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char help[] = "Usage: ps\n"
			   "List processes from /proc.";

struct proc_info {
	pid_t pid;
	pid_t ppid;
	char state;
	char comm[64];
};

static bool all_digits(const char *s)
{
	if (!*s)
		return false;
	for (; *s; s++) {
		if (!isdigit((unsigned char)*s))
			return false;
	}
	return true;
}

static int read_stat(pid_t pid, struct proc_info *info)
{
	char path[PATH_MAX];
	char buf[512];
	int fd;
	ssize_t n;

	snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';

	char *lp = strchr(buf, '(');
	char *rp = strrchr(buf, ')');

	if (!lp || !rp || rp < lp)
		return -1;

	size_t len = (size_t)(rp - lp - 1);

	if (len >= sizeof(info->comm))
		len = sizeof(info->comm) - 1;
	memcpy(info->comm, lp + 1, len);
	info->comm[len] = '\0';

	char *p = rp + 1;

	while (*p == ' ')
		p++;
	if (!*p)
		return -1;
	info->state = *p++;
	while (*p == ' ')
		p++;

	char *end;
	unsigned long ppid = strtoul(p, &end, 10);

	if (end == p)
		return -1;
	info->pid = pid;
	info->ppid = (pid_t)ppid;
	return 0;
}

static int cmp_pid(const void *a, const void *b)
{
	const struct proc_info *x = a;
	const struct proc_info *y = b;

	if (x->pid < y->pid)
		return -1;
	if (x->pid > y->pid)
		return 1;
	return 0;
}

int main(int argc, char *argv[])
{
	struct proc_info *infos = NULL;
	size_t nr = 0;
	size_t cap = 0;
	char dbuf[4096];
	int dfd;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		app_help_exit(help);
	if (argc != 1)
		app_usage(help);

	dfd = open("/proc", O_RDONLY | O_DIRECTORY);
	if (dfd < 0)
		return app_fail_errno("/proc");

	for (;;) {
		ssize_t n = getdents64(dfd, dbuf, sizeof(dbuf));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			ret = app_fail_errno("getdents64 /proc");
			break;
		}
		if (n == 0)
			break;

		for (size_t pos = 0; pos < (size_t)n;) {
			struct dirent64 *d = (struct dirent64 *)(dbuf + pos);

			if (d->d_reclen == 0)
				break;
			pos += d->d_reclen;

			if (!all_digits(d->d_name))
				continue;

			long pid = strtol(d->d_name, NULL, 10);

			if (pid <= 0)
				continue;

			struct proc_info info;

			if (read_stat((pid_t)pid, &info) != 0)
				continue;

			if (nr == cap) {
				size_t newcap = cap ? cap * 2 : 16;
				struct proc_info *newinfos = realloc(
					infos, newcap * sizeof(*newinfos));

				if (!newinfos) {
					ret = app_fail("out of memory");
					close(dfd);
					free(infos);
					return ret;
				}
				infos = newinfos;
				cap = newcap;
			}
			infos[nr++] = info;
		}
	}
	close(dfd);

	if (nr > 1)
		qsort(infos, nr, sizeof(*infos), cmp_pid);

	puts("  PID  PPID STAT COMMAND");
	for (size_t i = 0; i < nr; i++)
		printf("%5ld %5ld %c %s\n", (long)infos[i].pid,
		       (long)infos[i].ppid, infos[i].state, infos[i].comm);

	free(infos);
	return ret;
}
