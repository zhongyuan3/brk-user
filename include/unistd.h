#ifndef _UNISTD_H
#define _UNISTD_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
off_t lseek(int fd, off_t offset, int whence);

int execve(const char *path, char *const argv[], char *const envp[]);
int execv(const char *path, char *const argv[]);
int execvp(const char *file, char *const argv[]);
int execvpe(const char *file, char *const argv[], char *const envp[]);

pid_t getpid(void);
pid_t getppid(void);

int chdir(const char *path);
int fchdir(int fd);
char *getcwd(char *buf, size_t size);

int link(const char *oldpath, const char *newpath);
int linkat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath,
	   int flags);
int unlink(const char *path);
int unlinkat(int dirfd, const char *path, int flags);

int pipe(int pipefd[2]);
int pipe2(int pipefd[2], int flags);

int brk(void *addr);
void *sbrk(intptr_t increment);

ssize_t getdents64(int fd, void *dirp, size_t count);

pid_t fork(void);

void _exit(int status) __attribute__((noreturn));

unsigned int sleep(unsigned int seconds);

void sync(void);
int fsync(int fd);

extern char **environ;

#endif
