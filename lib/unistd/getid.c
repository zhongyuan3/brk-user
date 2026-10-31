#include <syscall.h>
#include <unistd.h>

uid_t getuid(void)
{
	return (uid_t)syscall(SYS_getuid);
}

uid_t geteuid(void)
{
	return (uid_t)syscall(SYS_geteuid);
}

gid_t getgid(void)
{
	return (gid_t)syscall(SYS_getgid);
}

gid_t getegid(void)
{
	return (gid_t)syscall(SYS_getegid);
}

int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
	return syscall(SYS_getresuid, ruid, euid, suid);
}

int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
	return syscall(SYS_getresgid, rgid, egid, sgid);
}
