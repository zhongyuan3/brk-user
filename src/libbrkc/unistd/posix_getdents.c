#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>

#define POSIX_DENT_ALIGN 8

ssize_t posix_getdents(int fd, void *dirp, size_t count, int flags)
{
	size_t raw_size;
	ssize_t n;
	char *raw;
	size_t in = 0;
	size_t out = 0;

	if (flags) {
		errno = EINVAL;
		return -1;
	}
	if (!dirp && count) {
		errno = EFAULT;
		return -1;
	}
	if (count < sizeof(struct posix_dent))
		return 0;

	/*
	 * A raw entry needs at least 16 bytes and a POSIX entry at least 24.
	 * Ask the kernel for only two thirds of the caller's buffer so the
	 * converted output always fits without a second pass.
	 */
	raw_size = count * 2 / 3;
	raw = malloc(raw_size);
	if (!raw) {
		errno = ENOMEM;
		return -1;
	}

	n = syscall(SYS_getdents, fd, raw, raw_size, 0);
	if (n < 0) {
		free(raw);
		return -1;
	}

	while (in < (size_t)n) {
		const struct brk_dirent *de =
			(const struct brk_dirent *)(raw + in);
		size_t max_name;
		size_t namelen;
		size_t len;
		struct posix_dent *dent;

		if (de->d_reclen < offsetof(struct brk_dirent, d_name) + 1 ||
		    in + de->d_reclen > (size_t)n)
			break;

		max_name = de->d_reclen - offsetof(struct brk_dirent, d_name);
		namelen = strnlen(de->d_name, max_name);

		len = offsetof(struct posix_dent, d_name) + namelen + 1;
		len = (len + POSIX_DENT_ALIGN - 1) & ~(POSIX_DENT_ALIGN - 1);
		if (out + len > count)
			break;

		dent = (struct posix_dent *)((char *)dirp + out);
		dent->d_ino = de->d_ino;
		dent->d_reclen = len;
		dent->d_type = de->d_type;
		memcpy(dent->d_name, de->d_name, namelen);
		dent->d_name[namelen] = '\0';

		in += de->d_reclen;
		out += len;
	}

	free(raw);
	return (ssize_t)out;
}
