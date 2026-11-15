#include <cc.h>
#include "pp.h"

#define BASE 3
#define SUM (BASE + PP_VALUE)

#ifndef BASE
#error BASE should be defined
#endif

#if defined(PP_VALUE) && PP_VALUE > 0
#define OK 1
#else
#define OK 0
#endif

#if 0
this line must be skipped
#endif

#ifdef MISSING
#error this must not be evaluated
#endif

void putnum(int n)
{
	if (n < 0) {
		putchar('-');
		n = -n;
	}
	if (n >= 10)
		putnum(n / 10);
	putchar('0' + n % 10);
}

int pp_add(int a, int b)
{
	return a + b;
}

int main(void)
{
	puts("pp-ok");
	putnum(SUM);
	putchar('\n');
	putnum(OK);
	putchar('\n');
	putnum(pp_add(SUM, 2));
	putchar('\n');
	return 0;
}
