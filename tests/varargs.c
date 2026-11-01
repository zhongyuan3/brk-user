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

int sum(int n, ...)
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

long suml(int n, ...)
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

void print_all(int n, ...)
{
	va_list ap;
	int i;

	va_start(ap, n);
	for (i = 0; i < n; i++) {
		char *p = va_arg(ap, char *);

		puts(p);
	}
	va_end(ap);
}

void show(const char *fmt, ...)
{
	va_list ap;
	va_list copy;

	va_start(ap, fmt);
	va_copy(copy, ap);
	for (; *fmt; fmt++) {
		if (*fmt == 'd') {
			putnum(va_arg(ap, int));
		} else if (*fmt == 's') {
			char *p = va_arg(ap, char *);

			while (*p)
				putchar(*p++);
		} else {
			putchar(*fmt);
		}
	}
	va_end(copy);
	va_end(ap);
}

int main(void)
{
	int (*fp)(int, ...) = sum;

	putnum(sum(3, 10, 20, 12));
	putchar('\n');
	putnum(sum(5, 1, 2, 3, 4, 5));
	putchar('\n');
	putnum(sum(0));
	putchar('\n');
	putnum(fp(2, 7, 8));
	putchar('\n');

	putnum(suml(2, 5000000000, 1000000000) == 6000000000);
	putchar('\n');

	print_all(3, "alpha", "beta", "gamma");
	show("d-s-d\n", 5, "mid", 7);

	return 0;
}
