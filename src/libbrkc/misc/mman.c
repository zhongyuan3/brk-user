#include <errno.h>
#include <sys/mman.h>
#include <syscall.h>

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	long ret = syscall(SYS_mmap, addr, length, prot, flags, fd, offset);

	if (ret == -1)
		return MAP_FAILED;
	return (void *)ret;
}

int munmap(void *addr, size_t length)
{
	return syscall(SYS_munmap, addr, length);
}

int mprotect(void *addr, size_t length, int prot)
{
	return syscall(SYS_mprotect, addr, length, prot);
}

int msync(void *addr, size_t length, int flags)
{
	return syscall(SYS_msync, addr, length, flags);
}

int mlock(const void *addr, size_t length)
{
	return syscall(SYS_mlock, addr, length);
}

int munlock(const void *addr, size_t length)
{
	return syscall(SYS_munlock, addr, length);
}

int mlockall(int flags)
{
	return syscall(SYS_mlockall, flags);
}

int munlockall(void)
{
	return syscall(SYS_munlockall);
}
