#include <sys/statvfs.h>
#include <syscall.h>

int statvfs(const char *path, struct statvfs *buf)
{
	return syscall(SYS_statvfs, path, buf);
}

int fstatvfs(int fd, struct statvfs *buf)
{
	return syscall(SYS_fstatvfs, fd, buf);
}
