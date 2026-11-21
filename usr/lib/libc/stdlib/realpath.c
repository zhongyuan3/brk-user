#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * realpath(): resolve path to its canonical absolute form, with
 * every symlink component expanded and "."/".." folded lexically.
 *
 * The walk appends one component at a time to a working buffer and
 * lstat's the result: when the newest component is a symlink, the
 * buffer is truncated back to its directory, the target is glued
 * onto the not-yet-walked remainder, and the walk restarts on that
 * concatenation (bounded to 40 expansions).  Relative paths start
 * from getcwd(); a missing component fails with the errno of the
 * failed lookup.
 */

#define REALPATH_LINKS 40

static int walk(char *buf, size_t bufsize, size_t *len, const char *rest,
		int *links);

static size_t parent_len(const char *buf, size_t len)
{
	while (len > 1 && buf[len - 1] != '/')
		len--;
	if (len > 1)
		len--;			/* strip the slash: /foo -> / */
	return len ? len : 1;
}

static int append(char *buf, size_t bufsize, size_t *len,
		  const char *comp, size_t clen)
{
	if (*len > 1 && buf[*len - 1] != '/') {
		if (*len + 1 >= bufsize)
			return -ENAMETOOLONG;
		buf[(*len)++] = '/';
	}
	if (*len + clen >= bufsize)
		return -ENAMETOOLONG;
	memcpy(buf + *len, comp, clen);
	*len += clen;
	buf[*len] = '\0';
	return 0;
}

static int walk(char *buf, size_t bufsize, size_t *len, const char *rest,
		int *links)
{
	const char *p, *start;

	for (p = start = rest;; p++) {
		if (*p == '/' || !*p) {
			size_t clen = (size_t)(p - start);

			if (clen && strncmp(start, ".", clen)) {
				struct stat st;

				if (!strncmp(start, "..", clen)) {
					*len = parent_len(buf, *len);
					buf[*len] = '\0';
				} else {
					int err = append(buf, bufsize, len,
							start, clen);

					if (err) {
						errno = -err;
						return -1;
					}
					if (lstat(buf, &st) < 0)
						return -1;
					if (S_ISLNK(st.st_mode)) {
						char target[PATH_MAX];
						char merged[PATH_MAX];
						ssize_t n;

						if (++*links >
						    REALPATH_LINKS) {
							errno = ELOOP;
							return -1;
						}
						n = readlink(buf, target,
							     sizeof(target) -
								1);
						if (n < 0)
							return -1;
						target[n] = '\0';
						/* drop the symlink tail */
						*len = parent_len(buf, *len);
						buf[*len] = '\0';
						/* glue target + rest */
						if (snprintf(merged,
							      sizeof(merged),
							      "%s%s%s",
							      target,
							      *p ? "/" : "",
							      *p ? p + 1
								 : "") >=
						    (int)sizeof(merged)) {
							errno = ENAMETOOLONG;
							return -1;
						}
						if (merged[0] == '/') {
							*len = 1;
							buf[0] = '/';
							buf[1] = '\0';
							return walk(buf, bufsize,
								    len,
								    merged + 1,
								    links);
						}
						return walk(buf, bufsize, len,
							     merged, links);
					}
				}
			}
			if (!*p)
				return 0;
			start = p + 1;
		}
	}
}

char *realpath(const char *path, char *resolved)
{
	char buf[PATH_MAX];
	size_t len;
	int links = 0;

	if (!path || !*path) {
		errno = EINVAL;
		return NULL;
	}
	if (path[0] == '/') {
		len = 1;
		buf[0] = '/';
		buf[1] = '\0';
		path++;
	} else {
		if (!getcwd(buf, sizeof(buf)))
			return NULL;
		len = strlen(buf);
		if (walk(buf, sizeof(buf), &len, "", &links) < 0)
			return NULL;
	}
	if (walk(buf, sizeof(buf), &len, path, &links) < 0)
		return NULL;
	if (len == 0) {
		buf[0] = '/';
		buf[1] = '\0';
		len = 1;
	}
	{
		char *out = resolved ? resolved : malloc(len + 1);

		if (!out)
			return NULL;
		memcpy(out, buf, len + 1);
		return out;
	}
}
