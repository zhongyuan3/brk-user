#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Expected output:
 *   55
 *   45
 *   78
 *   550
 *   155
 *   24
 */

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

void putnl(void)
{
	putchar('\n');
}

int sum10(int a, int b, int c, int d, int e, int f, int g, int h, int i,
	  int j)
{
	return a + b + c + d + e + f + g + h + i + j;
}

long sum12(int a, long b, char c, short d, unsigned int e, long f, int g,
	   int h, int i, int j, int k, int l)
{
	return a + b + c + d + (long)e + f + g + h + i + j + k + l;
}

double dsum(int n, ...)
{
	va_list ap;
	double sum = 0;
	int i;

	va_start(ap, n);
	for (i = 0; i < n; i++)
		sum += va_arg(ap, double);
	va_end(ap);
	return sum;
}

int isum(int n, ...)
{
	va_list ap;
	int sum = 0;
	int i;

	va_start(ap, n);
	for (i = 0; i < n; i++)
		sum += va_arg(ap, int);
	va_end(ap);
	return sum;
}

long mix10(int a, double b, int c, float d, long e, int f, int g, int h,
	   int i, int j)
{
	return a + (long)b + c + (long)d + e + f + g + h + i + j;
}

int main(void)
{
	putnum(sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
	putnl();

	putnum((int)dsum(9, (float)1, (float)2, (float)3, (float)4,
			 (float)5, (float)6, (float)7, (float)8,
			 (float)9));
	putnl();

	putnum(sum12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12));
	putnl();

	putnum((int)mix10(10, (double)20, 30, (float)40, 50, 60, 70, 80,
			  90, 100));
	putnl();

	putnum(isum(10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20));
	putnl();

	putnum((int)(dsum(4, (float)3 / 2, (float)5 / 2,
			  (float)7 / 2, (float)9 / 2) *
		     2));
	putnl();

	return 0;
}
