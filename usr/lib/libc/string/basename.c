#include <string.h>

/*
 * basename() over a possibly empty or trailing-slash path, POSIX
 * semantics: the returned pointer may lie inside the buffer, "" maps
 * to ".", and a path of only slashes maps to "/".
 */
char *basename(char *path)
{
	char *p;

	if (!path || !*path)
		return (char *)".";
	if (!strcmp(path, "/"))
		return path;

	p = path + strlen(path);
	while (p > path + 1 && p[-1] == '/')
		*--p = '\0';
	p = strrchr(path, '/');
	if (!p)
		return path;
	*p = '\0';
	return p + 1;
}
