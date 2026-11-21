#include <string.h>
#include <sys/statvfs.h>
#include <uapi/statvfs.h>
#include <syscall.h>

static void statfs_to_statvfs(const struct __brk_statfs *st, struct statvfs *buf)
{
	memset(buf, 0, sizeof(*buf));

	buf->f_bsize = (__brk_ulong_t)st->bsize;
	buf->f_frsize = (__brk_ulong_t)st->frsize;
	buf->f_blocks = (__brk_ulong_t)st->blocks;
	buf->f_bfree = (__brk_ulong_t)st->bfree;
	buf->f_bavail = (__brk_ulong_t)st->bavail;
	buf->f_files = (__brk_ulong_t)st->files;
	buf->f_ffree = (__brk_ulong_t)st->ffree;
	buf->f_favail = (__brk_ulong_t)st->ffree;
	buf->f_fsid = (__brk_ulong_t)st->fsid;
	buf->f_namemax = (__brk_ulong_t)st->namelen;

	/* Internal mount flags use the MS_* numbering. */
	if (st->flags & 0x1)
		buf->f_flag |= ST_RDONLY;
	if (st->flags & 0x2)
		buf->f_flag |= ST_NOSUID;
}

int statvfs(const char *path, struct statvfs *buf)
{
	struct __brk_statfs st;

	if (syscall(SYS_STAT_FS, path, &st) < 0)
		return -1;

	statfs_to_statvfs(&st, buf);
	return 0;
}

int fstatvfs(int fd, struct statvfs *buf)
{
	struct __brk_statfs st;

	if (syscall(SYS_STAT_FS_FD, fd, &st) < 0)
		return -1;

	statfs_to_statvfs(&st, buf);
	return 0;
}
