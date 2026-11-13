#include <brk/limits.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * POSIX execvp()/execvpe() run a file that is not a valid executable image
 * through the shell.  The BRK kernel only loads ELF images, so this fallback
 * is also what makes shebang-less scripts usable as commands.
 */
static int exec_shell_script(const char *path, char *const argv[],
			     char *const envp[])
{
	size_t argc = 0;
	size_t nr_args;
	char **new_argv;

	if (!argv)
		return -1;

	while (argv[argc])
		argc++;

	nr_args = 2 + (argc > 1 ? argc - 1 : 0) + 1;
	new_argv = malloc(nr_args * sizeof(*new_argv));
	if (!new_argv) {
		errno = ENOMEM;
		return -1;
	}

	new_argv[0] = (char *)"/bin/sh";
	new_argv[1] = (char *)path;
	size_t n = 2;

	for (size_t i = 1; i < argc; i++)
		new_argv[n++] = argv[i];
	new_argv[n] = NULL;

	execve("/bin/sh", new_argv, envp);
	free(new_argv);
	return -1;
}

int execvpe(const char *file, char *const argv[], char *const envp[])
{
	char path_buf[PATH_MAX];
	const char *path;
	const char *p;
	size_t file_len;
	int got_eacces = 0;
	int got_nametoolong = 0;

	if (!file || !file[0]) {
		errno = ENOENT;
		return -1;
	}

	/*
	 * A slash means the caller supplied a pathname.  PATH must not be
	 * consulted in that case.
	 */
	if (strchr(file, '/')) {
		execve(file, argv, envp);
		if (errno == ENOEXEC && argv)
			return exec_shell_script(file, argv, envp);
		return -1;
	}

	file_len = strlen(file);
	if (file_len >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}

	/*
	 * Unlike the old implementation, do not try execve(file) first: for a
	 * bare name that resolves relative to the current directory and would
	 * make CWD an implicit first PATH entry.
	 */
	path = getenv("PATH");
	if (!path)
		path = "/bin:/sbin:/usr/bin:/usr/sbin";

	p = path;
	for (;;) {
		const char *e = strchr(p, ':');
		size_t len;
		size_t start;

		if (!e)
			e = p + strlen(p);
		len = (size_t)(e - p);

		if (len == 0) {
			start = 2;
			if (file_len + start + 1 > PATH_MAX) {
				errno = ENAMETOOLONG;
				got_nametoolong = 1;
				if (!*e)
					break;
				p = e + 1;
				continue;
			}
			path_buf[0] = '.';
			path_buf[1] = '/';
		} else {
			start = len + 1;
			if (len >= PATH_MAX ||
			    file_len + start + 1 > PATH_MAX) {
				errno = ENAMETOOLONG;
				got_nametoolong = 1;
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
		if (errno == EACCES)
			got_eacces = 1;
		else if (errno == ENAMETOOLONG)
			got_nametoolong = 1;
		else if (errno == ENOEXEC && argv)
			return exec_shell_script(path_buf, argv, envp);

		if (!*e)
			break;
		p = e + 1;
	}

	if (got_eacces)
		errno = EACCES;
	else if (got_nametoolong)
		errno = ENAMETOOLONG;
	return -1;
}
