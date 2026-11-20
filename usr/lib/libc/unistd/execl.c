#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

static int exec_varargs(const char *path, const char *arg, va_list ap,
			int use_path, int has_envp)
{
	va_list aq;
	size_t argc = 0;
	char **argv;
	char **envp = environ;
	int ret;

	va_copy(aq, ap);
	if (arg) {
		argc = 1;
		while (va_arg(aq, char *) != NULL)
			argc++;
	}
	va_end(aq);

	argv = malloc((argc + 1) * sizeof(*argv));
	if (!argv) {
		errno = ENOMEM;
		return -1;
	}

	if (arg) {
		argv[0] = (char *)arg;
		for (size_t i = 1; i < argc; i++)
			argv[i] = va_arg(ap, char *);
		(void)va_arg(ap, char *);
	}
	argv[argc] = NULL;

	if (has_envp)
		envp = va_arg(ap, char **);

	if (use_path)
		ret = execvp(path, argv);
	else
		ret = execve(path, argv, envp);

	free(argv);
	return ret;
}

int execl(const char *path, const char *arg, ...)
{
	va_list ap;
	int ret;

	va_start(ap, arg);
	ret = exec_varargs(path, arg, ap, 0, 0);
	va_end(ap);
	return ret;
}

int execle(const char *path, const char *arg, ...)
{
	va_list ap;
	int ret;

	va_start(ap, arg);
	ret = exec_varargs(path, arg, ap, 0, 1);
	va_end(ap);
	return ret;
}

int execlp(const char *file, const char *arg, ...)
{
	va_list ap;
	int ret;

	va_start(ap, arg);
	ret = exec_varargs(file, arg, ap, 1, 0);
	va_end(ap);
	return ret;
}
