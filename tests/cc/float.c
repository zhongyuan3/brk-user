#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * float/double code generation: arithmetic, comparisons, casts,
 * increment/decrement, compound assignment and conditions.
 * Expected output:
 *   4 5 7 4 9
 *   3 4 1 1 1 1 0
 *   8 6
 *   3 3 3
 *   1 1 1
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

float addf(float a, float b)
{
	return a + b;
}

double addd(double a, double b)
{
	return a + b;
}

float half(float x)
{
	return x / 2.0f;
}

int main(void)
{
	float f;
	double d;

	putnum((int)(1.5f + 2.5f));
	putchar(' ');
	putnum((int)(7.5f - 2.25f));
	putchar(' ');
	putnum((int)(3.0f * 2.5f));
	putchar(' ');
	putnum((int)(9.0f / 2.0f));
	putchar(' ');
	d = 2.25;
	putnum((int)(d * 4.0));
	putchar('\n');

	putnum((int)addf(1.25f, 2.25f));
	putchar(' ');
	putnum((int)addd(1.5, 2.75));
	putchar(' ');
	putnum(1.25f < 2.5f);
	putchar(' ');
	putnum(1.25f <= 1.25f);
	putchar(' ');
	putnum(3.5f > 2.5f);
	putchar(' ');
	putnum(3.5f >= 3.5f);
	putchar(' ');
	putnum(1.0f == 2.0f);
	putchar('\n');

	f = 10.0f;
	f += 2.5f;
	f *= 2.0f;
	f -= 1.0f;
	f /= 3.0f;
	putnum((int)f);
	putchar(' ');

	f = 5.5f;
	f++;
	++f;
	f--;
	putnum((int)f);
	putchar('\n');

	putnum((int)3.9);
	putchar(' ');
	putnum((int)(double)3);
	putchar(' ');
	putnum((int)half(7.0f));
	putchar('\n');

	if (0.0f)
		putnum(0);
	else
		putnum(1);
	putchar(' ');

	if (!0.0f)
		putnum(1);
	else
		putnum(0);
	putchar(' ');

	putnum(2.5f && 1);
	putchar('\n');

	return 0;
}
