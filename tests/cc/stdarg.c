#include <stdarg.h>

int puts(const char *s);
int putchar(int c);

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

long sum(int n, ...)
{
	va_list ap;
	long s = 0;
	int i;

	va_start(ap, n);
	for (i = 0; i < n; i++)
		s += va_arg(ap, long);
	va_end(ap);
	return s;
}

double dsum(int n, ...)
{
	va_list ap;
	double s = 0;
	int i;

	va_start(ap, n);
	for (i = 0; i < n; i++)
		s += va_arg(ap, double);
	va_end(ap);
	return s;
}

int main(void)
{
	putnum((int)sum(4, 1L, 2L, 3L, 4L));
	putchar(' ');
	putnum((int)(dsum(3, 1.5, 2.5, 3.0) * 2));
	putchar('\n');
	return 0;
}
