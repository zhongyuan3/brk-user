/*
 * Floating-point casts truncate toward zero (not round), and local
 * double negation uses fsgnjn rather than an FCVT encoding.
 * Expected output:
 *   -2 1 -1
 *   1 1
 */
#include <cc.h>

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

int main(void)
{
	double d = -2.5;
	float f = 1.5f;
	double x = d + f;

	putnum((int)d);
	putchar(' ');
	putnum((int)f);
	putchar(' ');
	putnum((int)x);
	putchar('\n');
	putnum((int)(double)1.5);
	putchar(' ');
	putnum((int)(float)1.5f);
	putchar('\n');
	return 0;
}
