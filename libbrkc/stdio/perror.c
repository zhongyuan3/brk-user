#include <errno.h>
#include <stdio.h>
#include <string.h>

void perror(const char *msg)
{
	const char *errmsg = strerror(errno);
	if (msg && *msg) {
		fwrite(msg, 1, strlen(msg), stderr);
		fputc(':', stderr);
		fputc(' ', stderr);
	}
	fwrite(errmsg, 1, strlen(errmsg), stderr);
	fputc('\n', stderr);
}
