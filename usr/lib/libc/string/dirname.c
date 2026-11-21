#include <string.h>

/*
 * dirname() with POSIX semantics: "" maps to ".", a path with no
 * slash maps to ".", and trailing slashes are dropped before the
 * last significant component is removed.  The returned pointer may
 * point at a static string.
 */
char *dirname(char *path)
{
	static char dot[] = ".";
	static char slash[] = "/";
	char *p;

	if (!path || !*path)
		return dot;

	p = path + strlen(path);
	while (p > path && p[-1] == '/')
		p--;
	*p = '\0';

	p = strrchr(path, '/');
	if (!p)
		return dot;
	if (p == path)
		return slash;
	while (p > path && p[-1] == '/')
		p--;
	*p = '\0';
	return path;
}
