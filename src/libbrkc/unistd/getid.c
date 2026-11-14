#include <syscall.h>
#include <unistd.h>

uid_t getuid(void)
{
	uid_t ruid, euid, suid;

	if (getresuid(&ruid, &euid, &suid) != 0)
		return (uid_t)-1;
	return ruid;
}

uid_t geteuid(void)
{
	uid_t ruid, euid, suid;

	if (getresuid(&ruid, &euid, &suid) != 0)
		return (uid_t)-1;
	return euid;
}

gid_t getgid(void)
{
	gid_t rgid, egid, sgid;

	if (getresgid(&rgid, &egid, &sgid) != 0)
		return (gid_t)-1;
	return rgid;
}

gid_t getegid(void)
{
	gid_t rgid, egid, sgid;

	if (getresgid(&rgid, &egid, &sgid) != 0)
		return (gid_t)-1;
	return egid;
}

int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
	return syscall(SYS_getresuid, ruid, euid, suid);
}

int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
	return syscall(SYS_getresgid, rgid, egid, sgid);
}

int getgroups(int size, gid_t list[])
{
	return (int)syscall(SYS_getgroups, size, list);
}
