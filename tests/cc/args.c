#include <cc.h>

/*
 * Argument slots: more than eight parameters, stack arguments,
 * floating-point slots, an aggregate in a stack slot and varargs.
 * Expected output:
 *   55
 *   55
 *   39
 *   43
 *   10
 *   45
 */

struct pair {
	int a;
	int b;
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

int sum10(int a, int b, int c, int d, int e, int f, int g, int h, int i,
	  int j)
{
	return a + b + c + d + e + f + g + h + i + j;
}

int stack_mix(int a, int b, int c, int d, int e, int f, int g, int h,
	      double x, float y)
{
	return a + b + c + d + e + f + g + h + (int)x + (int)y;
}

int pair_tail(int a, int b, int c, int d, int e, int f, int g, int h,
	      struct pair p)
{
	return a + b + c + d + e + f + g + h + p.a + p.b;
}

int var_sum(int n, ...)
{
	va_list ap;
	int s = 0;
	int i;

	va_start(ap, n);
	for (i = 0; i < n; i++)
		s += va_arg(ap, int);
	va_end(ap);
	return s;
}

int main(void)
{
	int (*fp)(int, int, int, int, int, int, int, int, int, int) = sum10;
	struct pair p;

	putnum(sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
	putchar('\n');

	putnum(fp(1, 2, 3, 4, 5, 6, 7, 8, 9, 10));
	putchar('\n');

	putnum(stack_mix(1, 2, 3, 4, 5, 6, 7, 8, 2.5, 1.5f));
	putchar('\n');

	p.a = 3;
	p.b = 4;
	putnum(pair_tail(1, 2, 3, 4, 5, 6, 7, 8, p));
	putchar('\n');

	putnum(var_sum(4, 1, 2, 3, 4));
	putchar('\n');

	putnum(var_sum(9, 1, 2, 3, 4, 5, 6, 7, 8, 9));
	putchar('\n');

	return 0;
}
