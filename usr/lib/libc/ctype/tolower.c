#include <ctype.h>

int tolower(int c)
{
	return ((unsigned)c | 32) - 'a' < 26 ? (c | 32) : c;
}
