#include <syscall.h>
#include <unistd.h>

int execve(const char *path, char *const argv[], char *const envp[])
{
	return syscall(SYS_execve, path, argv, envp);
}
