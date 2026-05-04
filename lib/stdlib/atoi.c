#include <ctype.h>
#include <stdlib.h>

int atoi(const char *nptr)
{
	bool neg = false;
	int acc = 0;
	while (isspace((unsigned char)*nptr))
		nptr++;
	if (*nptr == '+') {
		nptr++;
	} else if (*nptr == '-') {
		neg = true;
		nptr++;
	}
	while (isdigit((unsigned char)*nptr))
		acc = acc * 10 + (*nptr++ - '0');
	return neg ? -acc : acc;
}
