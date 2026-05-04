#include <syscall.h>
#include <unistd.h>

int brk(void *addr)
{
	return __syscall_ret(-ENOMEM); /* deprecated */
}
