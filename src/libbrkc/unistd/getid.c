#include <syscall.h>
#include <uapi/proc.h>
#include <unistd.h>

static int get_proc(struct brk_proc *proc)
{
	return (int)syscall(SYS_getproc, proc);
}

uid_t getuid(void)
{
	struct brk_proc proc;

	if (get_proc(&proc) < 0)
		return (uid_t)-1;
	return proc.uid;
}

uid_t geteuid(void)
{
	struct brk_proc proc;

	if (get_proc(&proc) < 0)
		return (uid_t)-1;
	return proc.euid;
}

gid_t getgid(void)
{
	struct brk_proc proc;

	if (get_proc(&proc) < 0)
		return (gid_t)-1;
	return proc.gid;
}

gid_t getegid(void)
{
	struct brk_proc proc;

	if (get_proc(&proc) < 0)
		return (gid_t)-1;
	return proc.egid;
}

int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
	struct brk_proc proc;

	if (get_proc(&proc) < 0)
		return -1;
	if (ruid)
		*ruid = proc.uid;
	if (euid)
		*euid = proc.euid;
	if (suid)
		*suid = proc.suid;
	return 0;
}

int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
	struct brk_proc proc;

	if (get_proc(&proc) < 0)
		return -1;
	if (rgid)
		*rgid = proc.gid;
	if (egid)
		*egid = proc.egid;
	if (sgid)
		*sgid = proc.sgid;
	return 0;
}

int getgroups(int size, gid_t list[])
{
	return (int)syscall(SYS_getgroups, size, list);
}
