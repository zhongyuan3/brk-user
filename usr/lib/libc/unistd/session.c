#include <syscall.h>
#include <uapi/proc.h>
#include <unistd.h>

/*
 * Session and process-group primitives.  getpgid()/getsid() can
 * query any process (pid 0 means the caller); the caller's own
 * values are also part of the GET_PROC snapshot.
 */
pid_t setsid(void)
{
	return (pid_t)syscall(SYS_SET_SID);
}

int setpgid(pid_t pid, pid_t pgid)
{
	return syscall(SYS_SET_PGID, pid, pgid);
}

pid_t getpgid(pid_t pid)
{
	return (pid_t)syscall(SYS_GET_PGID, pid);
}

pid_t getsid(pid_t pid)
{
	return (pid_t)syscall(SYS_GET_SID, pid);
}

pid_t getpgrp(void)
{
	struct brk_proc proc;

	if (syscall(SYS_GET_PROC, &proc) < 0)
		return (pid_t)-1;
	return proc.pgid;
}
