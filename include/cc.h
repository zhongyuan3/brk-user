#ifndef _CC_H
#define _CC_H

#define NULL 0
#define EOF (-1)

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 64
#define O_TRUNC 512
#define O_APPEND 1024

int write(int fd, const void *buf, long n);
int read(int fd, void *buf, long n);
int open(const char *path, int flags, int mode);
int close(int fd);
void exit(int status);
int putchar(int c);
int puts(const char *s);

typedef char *va_list;

char *__builtin_va_start(void);

#define va_start(ap, last) ((ap) = __builtin_va_start())
#define va_arg(ap, type) (*(type *)((ap += 8) - 8))
#define va_end(ap) ((void)0)
#define va_copy(dst, src) ((dst) = (src))

#endif
