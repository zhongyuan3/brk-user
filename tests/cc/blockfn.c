/*
 * Block-scope function declarations and function-pointer declarations.
 * Expected output:
 *   5 6 7
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

int later(int x)
{
	return x + 2;
}

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
	extern int later(int);
	int (*fp)(int);

	fp = later;
	putnum(later(3));
	putchar(' ');
	putnum(fp(4));
	putchar(' ');
	putnum(later(5));
	putchar('\n');
	return 0;
}
