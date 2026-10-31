#include <brk/limits.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void handle_sigint(int sig)
{
	(void)sig;
}

static int join_path(char *out, size_t size, const char *dir, const char *name)
{
	int n = snprintf(out, size, "%s/%s", dir, name);

	return n < 0 || (size_t)n >= size ? -1 : 0;
}

static void chown_tree(const char *path)
{
	int fd = open(path, O_RDONLY | O_DIRECTORY);

	if (fd < 0)
		return;

	char dbuf[4096];
	size_t used = 0;

	for (;;) {
		ssize_t rcnt = getdents64(fd, dbuf + used, sizeof(dbuf) - used);

		if (rcnt < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (rcnt == 0)
			break;
		used += (size_t)rcnt;

		struct dirent64 *d = (struct dirent64 *)dbuf;
		size_t pos = 0;

		while (pos < used) {
			if (d->d_reclen == 0)
				break;
			if (strcmp(d->d_name, ".") && strcmp(d->d_name, "..") &&
			    d->d_type != DT_LNK) {
				char child[PATH_MAX];

				if (join_path(child, sizeof(child), path,
					      d->d_name) == 0) {
					chown(child, 0, 0);
					if (d->d_type == DT_DIR)
						chown_tree(child);
				}
			}
			pos += d->d_reclen;
			d = (struct dirent64 *)((char *)d + d->d_reclen);
		}
		used = 0;
	}

	close(fd);
}

static void setup_users(void)
{
	chown_tree("/bin");
	chown("/bin", 0, 0);
	chmod("/bin", 0755);

	chown_tree("/etc");
	chown("/etc", 0, 0);
	chmod("/etc", 0755);

	mkdir("/root", 0700);
	chown("/root", 0, 0);
	chmod("/root", 0700);

	mkdir("/home", 0755);
	chown("/home", 0, 0);
	mkdir("/home/user", 0755);
	chown("/home/user", 1000, 1000);
	chmod("/home/user", 0755);

	chmod("/etc/shadow", 0600);
	chmod("/bin/su", 04755);
	chmod("/bin/login", 04755);
	chmod("/bin/passwd", 04755);
}

int main(void)
{
	setup_users();

	signal(SIGINT, handle_sigint);
	while (1) {
		pid_t cpid = fork();
		if (cpid < 0) {
			perror("fork failed");
			goto fail;
		}

		if (cpid == 0) {
			char *argv[] = { "sh", 0 };
			execvp(argv[0], argv);
			perror("execve failed");
			_exit(1);
		}

		while (1) {
			pid_t wpid = wait(0);
			if (wpid == cpid) {
				printf("\nPress any key to restart shell\n");
				getchar();
				break;
			}
		}
	}

fail:
	fprintf(stderr, "failed to start shell\n");
	while (1)
		wait(0);
}
