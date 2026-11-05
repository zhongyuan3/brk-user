#include <ctype.h>

int isxdigit(int c)
{
	return isdigit(c) || ('a' <= c && c <= 'f') || ('A' <= c && c <= 'F');
}
