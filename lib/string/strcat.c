#include <string.h>

char *strcat(char *dst, char const *src)
{
	strcpy(dst + strlen(dst), src);
	return dst;
}
