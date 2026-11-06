#include <errno.h>
#include <stddef.h>
#include <sys/wait.h>

pid_t waitpid(pid_t pid, int *wstatus, int options)
{
	siginfo_t info;
	idtype_t idtype;
	id_t id;
	int ret;

	if (pid == -1) {
		idtype = P_ALL;
		id = 0;
	} else if (pid > 0) {
		idtype = P_PID;
		id = pid;
	} else {
		errno = EINVAL;
		return -1;
	}

	ret = waitid(idtype, id, &info, options | WEXITED);
	if (ret < 0)
		return -1;
	if (info.si_signo == 0)
		return 0;

	if (wstatus) {
		if (info.si_code == CLD_EXITED)
			*wstatus = (info.si_status & 0xff) << 8;
		else
			*wstatus = info.si_status & 0x7f;
	}

	return info.si_pid;
}
