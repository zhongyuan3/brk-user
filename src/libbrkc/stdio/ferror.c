#include <stdio.h>

#include "stdio_impl.h"

int ferror(FILE *stream)
{
	return stream && (stream->flags & __IO_ERR);
}
