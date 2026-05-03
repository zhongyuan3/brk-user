#include "internal.h"
#include <brk/macros.h>
#include <brk/syscall.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char *environ[] = { NULL };
static char *sys_path[] = { "/", "/bin" };

/* Kernel ABI: success is zero or a non-negative value; errors are negative errno. */
static inline void errno_ok(void)
{
	errno = 0;
}

static inline int ret_err0(long err)
{
	if (err != 0) {
		errno = (int)(-err);
		return -1;
	}
	errno_ok();
	return 0;
}

static inline ssize_t ret_ssize(long v)
{
	if (v < 0) {
		errno = (int)(-v);
		return -1;
	}
	errno_ok();
	return (ssize_t)v;
}

static inline off_t ret_off(long v)
{
	if (v < 0) {
		errno = (int)(-v);
		return (off_t)-1;
	}
	errno_ok();
	return (off_t)v;
}

static inline int ret_fd(long fd)
{
	if (fd < 0) {
		errno = (int)(-fd);
		return -1;
	}
	errno_ok();
	return (int)fd;
}

static inline pid_t ret_pid(long v)
{
	if (v < 0) {
		errno = (int)(-v);
		return -1;
	}
	errno_ok();
	return (pid_t)v;
}

static inline clock_t ret_clock(long t)
{
	if (t < 0) {
		errno = (int)(-t);
		return -1;
	}
	errno_ok();
	return (clock_t)t;
}

static inline char *ret_getcwd(long err, char *buf)
{
	if (err != 0) {
		errno = (int)(-err);
		return NULL;
	}
	errno_ok();
	return buf;
}

ssize_t read(int fd, void *buf, size_t count)
{
	return ret_ssize(syscall(SYS_read, fd, buf, count));
}

ssize_t write(int fd, const void *buf, size_t count)
{
	return ret_ssize(syscall(SYS_write, fd, buf, count));
}

off_t lseek(int fd, off_t offset, int whence)
{
	return ret_off(syscall(SYS_lseek, fd, offset, whence));
}

void exit(int status)
{
	syscall(SYS_exit, status);
	errno_ok();
	for (;;)
		;
}

int open(const char *path, int flags, ...)
{
	return ret_fd(syscall(SYS_open, path, flags, 0));
}

int openat(int dirfd, const char *path, int flags, mode_t mode)
{
	return ret_fd(syscall(SYS_openat, dirfd, path, flags, mode));
}

int close(int fd)
{
	return ret_err0(syscall(SYS_close, fd));
}

int dup(int oldfd)
{
	return ret_fd(syscall(SYS_dup, oldfd));
}

int dup2(int oldfd, int newfd)
{
	return ret_fd(syscall(SYS_dup2, oldfd, newfd));
}

int execve(const char *path, char *const argv[], char *const envp[])
{
	return ret_err0(syscall(SYS_execve, path, argv, envp));
}

pid_t getpid(void)
{
	return ret_pid(syscall(SYS_getpid));
}

pid_t getppid(void)
{
	return ret_pid(syscall(SYS_getppid));
}

int mkdir(const char *path, mode_t mode)
{
	return ret_err0(syscall(SYS_mkdir, path, mode));
}

int mkdirat(int dirfd, const char *path, mode_t mode)
{
	return ret_err0(syscall(SYS_mkdirat, dirfd, path, mode));
}

int chdir(const char *path)
{
	return ret_err0(syscall(SYS_chdir, path));
}

char *getcwd(char *buf, size_t size)
{
	return ret_getcwd(syscall(SYS_getcwd, buf, size), buf);
}

int mknod(const char *path, mode_t mode, dev_t dev)
{
	return ret_err0(syscall(SYS_mknod, path, mode, dev));
}

int mknodat(int dirfd, const char *path, mode_t mode, dev_t dev)
{
	return ret_err0(syscall(SYS_mknodat, dirfd, path, mode, dev));
}

int link(const char *oldpath, const char *newpath)
{
	return ret_err0(syscall(SYS_link, oldpath, newpath));
}

int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath,
	   int flags)
{
	return ret_err0(syscall(SYS_linkat, olddirfd, oldpath, newdirfd,
				newpath, flags));
}

int unlink(const char *path)
{
	return ret_err0(syscall(SYS_unlink, path));
}

int unlinkat(int dirfd, const char *path, int flags)
{
	return ret_err0(syscall(SYS_unlinkat, dirfd, path, flags));
}

int pipe(int pipefd[2])
{
	return ret_err0(syscall(SYS_pipe, pipefd));
}

int pipe2(int pipefd[2], int flags)
{
	return ret_err0(syscall(SYS_pipe2, pipefd, flags));
}

int uname(struct utsname *buf)
{
	return ret_err0(syscall(SYS_uname, buf));
}

int brk(void *addr)
{
	if (!addr)
		return 0;
	return (int)syscall(SYS_brk, addr);
}

ssize_t getdents64(int fd, void *dirp, size_t count)
{
	return ret_ssize(syscall(SYS_getdents64, fd, dirp, count));
}

pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage)
{
	return ret_pid(syscall(SYS_wait4, pid, wstatus, options, rusage));
}

pid_t fork(void)
{
	return ret_pid(syscall(SYS_fork));
}

int nanosleep(const struct timespec *duration, struct timespec *rem)
{
	return ret_err0(syscall(SYS_nanosleep, duration, rem));
}

clock_t times(struct tms *buf)
{
	return ret_clock(syscall(SYS_times, buf));
}

int mount(const char *source, const char *target, const char *filesystemtype,
	  unsigned long mountflags, const void *data)
{
	return ret_err0(syscall(SYS_mount, source, target, filesystemtype,
				mountflags, data));
}

int umount2(const char *target, int flags)
{
	return ret_err0(syscall(SYS_umount2, target, flags));
}

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
	return ret_err0(syscall(SYS_gettimeofday, tv, tz));
}

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
	return ret_err0(syscall(SYS_settimeofday, tv, tz));
}

int fstat(int fd, struct stat *buf)
{
	return ret_err0(syscall(SYS_fstat, fd, buf));
}

int stat(const char *path, struct stat *buf)
{
	return ret_err0(syscall(SYS_stat, path, buf));
}

int lstat(const char *path, struct stat *buf)
{
	return ret_err0(syscall(SYS_lstat, path, buf));
}

pid_t wait(int *wstatus)
{
	return wait4(-1, wstatus, 0, 0);
}

pid_t waitpid(pid_t pid, int *wstatus, int options)
{
	return wait4(pid, wstatus, options, 0);
}

int execv(const char *path, char *const argv[])
{
	return execve(path, argv, environ);
}

int execvp(const char *file, char *const argv[])
{
	return execvpe(file, argv, environ);
}

int execvpe(const char *file, char *const argv[], char *const envp[])
{
	char path_buf[PATH_MAX];

	execve(file, argv, envp);

	size_t file_len = strlen(file);
	if (file_len >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}

	for (size_t i = 0; i < countof(sys_path); i++) {
		size_t sys_path_len = strlen(sys_path[i]);
		if (sys_path_len + file_len + 2 >= PATH_MAX) {
			errno = ENAMETOOLONG;
			return -1;
		}
		memcpy(path_buf, sys_path[i], sys_path_len);
		path_buf[sys_path_len] = '/';
		memcpy(path_buf + sys_path_len + 1, file, file_len);
		path_buf[sys_path_len + file_len + 1] = '\0';
		execve(path_buf, argv, envp);
	}

	return -1;
}

void *sbrk(intptr_t increment)
{
	static uint64_t curr_brk = 0;

	if (curr_brk == 0)
		curr_brk = syscall(SYS_brk, 0);
	uint64_t new_brk = (uint64_t)((intptr_t)curr_brk + increment);
	long ret = syscall(SYS_brk, new_brk);
	if (ret != 0) {
		errno = (int)(-ret);
		return (void *)-1;
	}
	uint64_t old_brk = curr_brk;
	curr_brk = new_brk;
	errno_ok();
	return (void *)old_brk;
}
