#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "stdio_impl.h"

static FILE *open_unique_tmp(const char *mode)
{
	unsigned long seq = 0;
	char name[64];

	for (;;) {
		snprintf(name, sizeof(name), "/tmp/tmp.%ld.%lu",
			 (long)getpid(), seq++);
		FILE *f = fopen(name, mode);

		if (f)
			return f;
		if (errno != EEXIST)
			return NULL;
		if (seq > 100000)
			return NULL;
	}
}

FILE *tmpfile(void)
{
	return open_unique_tmp("wb+");
}

char *tmpnam(char *s)
{
	static char static_buf[L_tmpnam];

	if (!s)
		s = static_buf;
	snprintf(s, L_tmpnam, "/tmp/lua.%ld.%lu", (long)getpid(),
		 (unsigned long)time(NULL));
	return s;
}
