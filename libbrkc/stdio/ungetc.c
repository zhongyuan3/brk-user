#include <stdio.h>

#include "stdio_impl.h"

int ungetc(int c, FILE *stream)
{
	if (!stream || (stream->flags & __IO_ERR))
		return EOF;
	if (c == EOF)
		return EOF;
	stream->unget = (char)c;
	stream->unget_valid = 1;
	/* A pushed-back character makes at least one byte readable again. */
	stream->flags &= (unsigned char)~__IO_EOF;
	return (int)(unsigned char)stream->unget;
}
