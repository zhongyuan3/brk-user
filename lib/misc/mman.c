#include <errno.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <syscall.h>

void *mmap(void *addr, size_t length, int prot, int flags, int fd,
	   off_t offset)
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

void *mremap(void *old_address, size_t old_size, size_t new_size, int flags,
	     ...)
{
	void *new_address = NULL;
	long ret;

	if (flags & MREMAP_FIXED) {
		va_list ap;

		va_start(ap, flags);
		new_address = va_arg(ap, void *);
		va_end(ap);
	}

	ret = syscall(SYS_mremap, old_address, old_size, new_size, flags,
		      new_address);
	if (ret == -1)
		return MAP_FAILED;
	return (void *)ret;
}
