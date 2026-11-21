#include <stdio.h>

/*
 * setbuf()/setbuffer(): convenience wrappers over setvbuf.  A NULL
 * buffer turns the stream unbuffered; otherwise the stream becomes
 * fully buffered with the caller's storage (BUFSIZ bytes unless
 * setbuffer overrides the size).
 */
void setbuf(FILE *stream, char *buf)
{
	setvbuf(stream, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

void setbuffer(FILE *stream, char *buf, size_t size)
{
	setvbuf(stream, buf, buf ? _IOFBF : _IONBF, size);
}
