#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

int access(const char *path, int mode);
int faccessat(int dirfd, const char *path, int mode, int flags);

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int dup3(int oldfd, int newfd, int flags);
off_t lseek(int fd, off_t offset, int whence);

int execve(const char *path, char *const argv[], char *const envp[]);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
int execvpe(const char *file, char *const argv[], char *const envp[]);
int execl(const char *path, const char *arg, ...);
int execle(const char *path, const char *arg, ...);
int execlp(const char *file, const char *arg, ...);

pid_t getpid(void);
pid_t getppid(void);

uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
int setreuid(uid_t ruid, uid_t euid);
int setregid(gid_t rgid, gid_t egid);
int setresuid(uid_t ruid, uid_t euid, uid_t suid);
int getresuid(uid_t *ruid, uid_t *euid, uid_t *suid);
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
int getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid);
int getgroups(int size, gid_t list[]);

int chown(const char *path, uid_t owner, gid_t group);
int fchown(int fd, uid_t owner, gid_t group);
int fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags);
int lchown(const char *path, uid_t owner, gid_t group);

int chdir(const char *path);
int fchdir(int fd);
char *getcwd(char *buf, size_t size);

int link(const char *oldpath, const char *newpath);
int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath,
	   int flags);
int symlink(const char *target, const char *linkpath);
int symlinkat(const char *target, int newdirfd, const char *linkpath);
ssize_t readlink(const char *path, char *buf, size_t bufsiz);
ssize_t readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz);
int unlink(const char *path);
int unlinkat(int dirfd, const char *path, int flags);
int rename(const char *oldpath, const char *newpath);
int renameat(int olddirfd, const char *oldpath, int newdirfd,
	     const char *newpath);
int rmdir(const char *path);

int pipe(int pipefd[2]);
int pipe2(int pipefd[2], int flags);

pid_t fork(void);

int pause(void);

void _exit(int status) __attribute__((noreturn));

unsigned int sleep(unsigned int seconds);

void sync(void);
int fsync(int fd);

extern char **environ;

#endif
