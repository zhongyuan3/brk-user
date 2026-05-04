#include <stdio.h>

int fputc(int c, FILE *stream)
{
	return fprintf(stream, "%c", c);
}
