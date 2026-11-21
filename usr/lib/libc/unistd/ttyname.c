#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * ttyname(): the pathname of the terminal attached to fd.
 *
 * The kernel exposes no fd-to-device table, so the device id from
 * fstat() is matched against the character devices in /dev.  The
 * plain form uses one static buffer; ttyname_r() returns 0 or an
 * error number, POSIX style.
 */
int ttyname_r(int fd, char *buf, size_t buflen)
{
	struct stat st;
	DIR *d;
	struct dirent *de;
	int err;

	if (fstat(fd, &st) < 0)
		return errno;
	if (!S_ISCHR(st.st_mode))
		return ENOTTY;

	d = opendir("/dev");
	if (!d)
		return errno;
	err = ENODEV;
	while ((de = readdir(d))) {
		char path[64];
		struct stat dst;

		snprintf(path, sizeof(path), "/dev/%s", de->d_name);
		if (stat(path, &dst) < 0)
			continue;
		if (S_ISCHR(dst.st_mode) && dst.st_rdev == st.st_rdev) {
			size_t need = strlen(path) + 1;

			if (need > buflen) {
				err = ERANGE;
				break;
			}
			memcpy(buf, path, need);
			err = 0;
			break;
		}
	}
	closedir(d);
	return err;
}

char *ttyname(int fd)
{
	static char buf[64];

	return ttyname_r(fd, buf, sizeof(buf)) == 0 ? buf : NULL;
}
