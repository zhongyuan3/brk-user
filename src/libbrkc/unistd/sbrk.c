#include <stdint.h>
#include <syscall.h>
#include <unistd.h>

static uintptr_t curr_brk = 0;

void *sbrk(intptr_t increment)
{
	if (curr_brk == 0)
		curr_brk = syscall(SYS_brk, 0);

	uintptr_t new_brk = (uintptr_t)((intptr_t)curr_brk + increment);
	if (syscall(SYS_brk, new_brk) < 0)
		return (void *)-1;

	uintptr_t old_brk = curr_brk;
	curr_brk = new_brk;
	return (void *)old_brk;
}
