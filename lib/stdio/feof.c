#include <stdio.h>

#include "stdio_impl.h"

int feof(FILE *stream)
{
	return stream && (stream->flags & __IO_EOF);
}
