#include <syscall.h>
#include <unistd.h>

int setuid(uid_t uid)
{
	return syscall(SYS_setuid, uid);
}

int setgid(gid_t gid)
{
	return syscall(SYS_setgid, gid);
}

int setreuid(uid_t ruid, uid_t euid)
{
	return syscall(SYS_setreuid, ruid, euid);
}

int setregid(gid_t rgid, gid_t egid)
{
	return syscall(SYS_setregid, rgid, egid);
}

int setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	return syscall(SYS_setresuid, ruid, euid, suid);
}

int setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
	return syscall(SYS_setresgid, rgid, egid, sgid);
}
