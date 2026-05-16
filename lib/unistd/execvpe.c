#include <brk/limits.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>

char **environ = NULL;

int execvpe(const char *file, char *const argv[], char *const envp[])
{
	char path_buf[PATH_MAX];

	execve(file, argv, envp);

	size_t file_len = strlen(file);
	if (file_len >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}

	char *path = "/bin:/usr/bin";
	char *p = path;
	char *e = NULL;

	for (; *p; p = e + 1) {
		e = strchr(p, ':');
		if (!e)
			e = p + strlen(p);
		size_t len = e - p;
		if (len + file_len + 2 >= PATH_MAX) {
			errno = ENAMETOOLONG;
			continue;
		}
		memcpy(path_buf, p, len);
		path_buf[len] = '/';
		memcpy(path_buf + len + 1, file, file_len);
		path_buf[len + file_len + 1] = '\0';
		execve(path_buf, argv, envp);
	}

	return -1;
}
