#include <brk/limits.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <unistd.h>

int execvpe(const char *file, char *const argv[], char *const envp[])
{
	char path_buf[PATH_MAX];

	execve(file, argv, envp);

	size_t file_len = strlen(file);
	if (file_len >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}

	const char *path = getenv("PATH");

	if (!path)
		path = "/bin:/sbin:/usr/bin:/usr/sbin";

	const char *p = path;

	for (;;) {
		const char *e = strchr(p, ':');
		size_t len;
		size_t start;

		if (!e)
			e = p + strlen(p);
		len = (size_t)(e - p);

		if (len == 0) {
			start = 2;
			if (file_len + start + 1 >= PATH_MAX) {
				errno = ENAMETOOLONG;
				if (!*e)
					break;
				p = e + 1;
				continue;
			}
			path_buf[0] = '.';
			path_buf[1] = '/';
		} else {
			start = len + 1;
			if (file_len + start + 1 >= PATH_MAX) {
				errno = ENAMETOOLONG;
				if (!*e)
					break;
				p = e + 1;
				continue;
			}
			memcpy(path_buf, p, len);
			path_buf[len] = '/';
		}
		memcpy(path_buf + start, file, file_len);
		path_buf[start + file_len] = '\0';
		execve(path_buf, argv, envp);

		if (!*e)
			break;
		p = e + 1;
	}

	return -1;
}
