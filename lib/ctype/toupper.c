#include <ctype.h>

int toupper(int c)
{
	return ((unsigned)c & ~32) - 'A' < 26 ? (c & ~32) : c;
}
