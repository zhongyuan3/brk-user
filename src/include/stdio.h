#ifndef _STDIO_H
#define _STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct __io_file FILE;

int printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int vprintf(const char *format, va_list ap);

int fprintf(FILE *stream, const char *format, ...)
	__attribute__((format(printf, 2, 3)));
int vfprintf(FILE *stream, const char *format, va_list ap);

int snprintf(char *buf, size_t size, char const *format, ...)
	__attribute__((format(printf, 3, 4)));
int vsnprintf(char *buf, size_t size, char const *format, va_list ap);

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int fflush(FILE *stream);
void clearerr(FILE *stream);
void rewind(FILE *stream);
int ferror(FILE *stream);
int feof(FILE *stream);

int putc(int c, FILE *stream);
int fputc(int c, FILE *stream);
int putchar(int c);

int fputs(const char *s, FILE *stream);
int puts(const char *s);

int getc(FILE *stream);
int fgetc(FILE *stream);
int getchar(void);
char *fgets(char *s, int size, FILE *stream);

void perror(const char *s);

ssize_t getline(char **lineptr, size_t *n, FILE *stream);
ssize_t getdelim(char **lineptr, size_t *n, int delimiter, FILE *stream);

extern FILE *__stdin_file;
extern FILE *__stdout_file;
extern FILE *__stderr_file;
#define stdin __stdin_file
#define stdout __stdout_file
#define stderr __stderr_file

#define EOF (-1)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#endif
