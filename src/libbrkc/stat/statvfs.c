#include <string.h>
#include <sys/statvfs.h>
#include <syscall.h>

static void statfs_to_statvfs(const struct brk_statfs *st, struct statvfs *buf)
{
	memset(buf, 0, sizeof(*buf));

	buf->f_bsize = (__kernel_ulong_t)st->bsize;
	buf->f_frsize = (__kernel_ulong_t)st->frsize;
	buf->f_blocks = (__kernel_ulong_t)st->blocks;
	buf->f_bfree = (__kernel_ulong_t)st->bfree;
	buf->f_bavail = (__kernel_ulong_t)st->bavail;
	buf->f_files = (__kernel_ulong_t)st->files;
	buf->f_ffree = (__kernel_ulong_t)st->ffree;
	buf->f_favail = (__kernel_ulong_t)st->ffree;
	buf->f_fsid = (__kernel_ulong_t)st->fsid;
	buf->f_namemax = (__kernel_ulong_t)st->namelen;

	/* Internal mount flags use the MS_* numbering. */
	if (st->flags & 0x1)
		buf->f_flag |= ST_RDONLY;
	if (st->flags & 0x2)
		buf->f_flag |= ST_NOSUID;
}

int statvfs(const char *path, struct statvfs *buf)
{
	struct brk_statfs st;

	if (syscall(SYS_statfs, path, &st) < 0)
		return -1;

	statfs_to_statvfs(&st, buf);
	return 0;
}

int fstatvfs(int fd, struct statvfs *buf)
{
	struct brk_statfs st;

	if (syscall(SYS_fstatfs, fd, &st) < 0)
		return -1;

	statfs_to_statvfs(&st, buf);
	return 0;
}
