#include <errno.h>
#include <syscall.h>
#include <unistd.h>

/*
 * The kernel exposes one setter per credential slot (real,
 * effective, saved): SET_UID_REAL, SET_UID_EFFECTIVE,
 * SET_UID_SAVED and the gid mirrors.  This file composes the
 * POSIX setuid/setreuid/setresuid family from those primitives.
 *
 * Permission decisions are made here, from the ids fetched via
 * GET_PROC, so the calls are pre-validated and never leave a
 * partially updated set of slots.  "Privileged" means a zero
 * effective id: the kernel holds the capability set full exactly
 * while euid is 0.
 */

int setuid(uid_t uid)
{
	uid_t r, e, s;

	if (getresuid(&r, &e, &s) != 0)
		return -1;

	if (e == 0) {
		if (syscall(SYS_SET_UID_REAL, uid))
			return -1;
		(void)syscall(SYS_SET_UID_EFFECTIVE, uid);
		(void)syscall(SYS_SET_UID_SAVED, uid);
		return 0;
	}

	if (uid != r && uid != s) {
		errno = EPERM;
		return -1;
	}
	return syscall(SYS_SET_UID_EFFECTIVE, uid);
}

int setgid(gid_t gid)
{
	gid_t r, e, s;

	if (getresgid(&r, &e, &s) != 0)
		return -1;

	if (e == 0) {
		if (syscall(SYS_SET_GID_REAL, gid))
			return -1;
		(void)syscall(SYS_SET_GID_EFFECTIVE, gid);
		(void)syscall(SYS_SET_GID_SAVED, gid);
		return 0;
	}

	if (gid != r && gid != s) {
		errno = EPERM;
		return -1;
	}
	return syscall(SYS_SET_GID_EFFECTIVE, gid);
}

int setreuid(uid_t ruid, uid_t euid)
{
	uid_t r, e, s;

	if (getresuid(&r, &e, &s) != 0)
		return -1;

	if (e != 0) {
		if (ruid != (uid_t)-1 && ruid != r && ruid != e) {
			errno = EPERM;
			return -1;
		}
		if (euid != (uid_t)-1 && euid != r && euid != e && euid != s) {
			errno = EPERM;
			return -1;
		}
	}

	if (ruid != (uid_t)-1 && syscall(SYS_SET_UID_REAL, ruid))
		return -1;
	if (euid != (uid_t)-1 && syscall(SYS_SET_UID_EFFECTIVE, euid))
		return -1;
	return 0;
}

int setregid(gid_t rgid, gid_t egid)
{
	gid_t r, e, s;

	if (getresgid(&r, &e, &s) != 0)
		return -1;

	if (e != 0) {
		if (rgid != (gid_t)-1 && rgid != r && rgid != e) {
			errno = EPERM;
			return -1;
		}
		if (egid != (gid_t)-1 && egid != r && egid != e && egid != s) {
			errno = EPERM;
			return -1;
		}
	}

	if (rgid != (gid_t)-1 && syscall(SYS_SET_GID_REAL, rgid))
		return -1;
	if (egid != (gid_t)-1 && syscall(SYS_SET_GID_EFFECTIVE, egid))
		return -1;
	return 0;
}

int setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	uid_t r, e, s;

	if (getresuid(&r, &e, &s) != 0)
		return -1;

	if (e != 0) {
		if (ruid != (uid_t)-1 && ruid != r && ruid != e && ruid != s) {
			errno = EPERM;
			return -1;
		}
		if (euid != (uid_t)-1 && euid != r && euid != e && euid != s) {
			errno = EPERM;
			return -1;
		}
		if (suid != (uid_t)-1 && suid != r && suid != e && suid != s) {
			errno = EPERM;
			return -1;
		}
	}

	if (ruid != (uid_t)-1 && syscall(SYS_SET_UID_REAL, ruid))
		return -1;
	if (euid != (uid_t)-1 && syscall(SYS_SET_UID_EFFECTIVE, euid))
		return -1;
	if (suid != (uid_t)-1 && syscall(SYS_SET_UID_SAVED, suid))
		return -1;
	return 0;
}

int setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
	gid_t r, e, s;

	if (getresgid(&r, &e, &s) != 0)
		return -1;

	if (e != 0) {
		if (rgid != (gid_t)-1 && rgid != r && rgid != e && rgid != s) {
			errno = EPERM;
			return -1;
		}
		if (egid != (gid_t)-1 && egid != r && egid != e && egid != s) {
			errno = EPERM;
			return -1;
		}
		if (sgid != (gid_t)-1 && sgid != r && sgid != e && sgid != s) {
			errno = EPERM;
			return -1;
		}
	}

	if (rgid != (gid_t)-1 && syscall(SYS_SET_GID_REAL, rgid))
		return -1;
	if (egid != (gid_t)-1 && syscall(SYS_SET_GID_EFFECTIVE, egid))
		return -1;
	if (sgid != (gid_t)-1 && syscall(SYS_SET_GID_SAVED, sgid))
		return -1;
	return 0;
}
