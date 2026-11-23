/*
 * Aggregate return combined with more than eight argument slots.
 * The hidden result pointer occupies slot 0, so the ninth explicit
 * argument lives on the caller stack.
 * Expected output:
 *   10 35
 *   30 105
 *   10 35
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

struct P {
	long a;
	long b;
};

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

struct P make(long a, long b, long c, long d, long e, long f, long g, long h,
	      long i)
{
	struct P p;

	p.a = a + i;
	p.b = b + c + d + e + f + g + h;
	return p;
}

int main(void)
{
	struct P p;
	struct P q;
	struct P (*fp)(long, long, long, long, long, long, long, long, long) =
		make;

	p = make(1, 2, 3, 4, 5, 6, 7, 8, 9);
	putnum((int)p.a);
	putchar(' ');
	putnum((int)p.b);
	putchar('\n');

	q = fp(11, 12, 13, 14, 15, 16, 17, 18, 19);
	putnum((int)q.a);
	putchar(' ');
	putnum((int)q.b);
	putchar('\n');

	putnum((int)(make(1, 2, 3, 4, 5, 6, 7, 8, 9).a));
	putchar(' ');
	putnum((int)(make(1, 2, 3, 4, 5, 6, 7, 8, 9).b));
	putchar('\n');

	return 0;
}
