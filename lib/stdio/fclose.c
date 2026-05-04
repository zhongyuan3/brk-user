#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int fclose(FILE *stream)
{
	if (!stream)
		return -1;
	if (stream == stdin || stream == stdout || stream == stderr)
		return -1;
	if (fflush(stream) < 0)
		return -1;
	close(stream->fd);
	free(stream->buf);
	free(stream);
	return 0;
}
