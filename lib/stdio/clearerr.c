#include <stdio.h>

#include "stdio_impl.h"

void clearerr(FILE *stream)
{
	if (!stream)
		return;
	stream->flags &= (unsigned char) ~(__IO_EOF | __IO_ERR);
}

void rewind(FILE *stream)
{
	if (!stream)
		return;
	clearerr(stream);
	(void)fseek(stream, 0, SEEK_SET);
}
