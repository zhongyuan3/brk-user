#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Expected output:
 *   1 2 3
 *   100
 *   10 20
 *   13 1 2 3
 *   13 1 2 3
 *   2 3 11
 *   7
 *   8
 *   5 6
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

void putsp(void)
{
	putchar(' ');
}

void putnl(void)
{
	putchar('\n');
}

int counter = 100;
static int hidden_static = 8;

int bump(void)
{
	static int counter = 0;

	return ++counter;
}

int add_ten(void)
{
	static int counter;

	counter += 10;
	return counter;
}

int get_static(void)
{
	static int value = 3 * 4 + 1;
	static int arr[3] = { 1, 2, 3 };
	static char msg[] = "hi";

	putnum(value);
	putsp();
	putnum(arr[0]);
	putsp();
	putnum(arr[1]);
	putsp();
	putnum(arr[2]);
	putnl();
	(void)msg;
	return value;
}

int nested_static(int which)
{
	if (which) {
		static int value = 1;

		return ++value;
	} else {
		static int value = 10;

		return ++value;
	}
}

int read_later(void)
{
	extern int later;

	return later;
}

int read_static_global(void)
{
	extern int hidden_static;

	return hidden_static;
}

int later = 7;

int main(void)
{
	static int seed = 5;
	int v;

	putnum(bump());
	putsp();
	putnum(bump());
	putsp();
	putnum(bump());
	putnl();

	putnum(counter);
	putnl();

	putnum(add_ten());
	putsp();
	putnum(add_ten());
	putnl();

	get_static();
	get_static();

	putnum(nested_static(1));
	putsp();
	putnum(nested_static(1));
	putsp();
	putnum(nested_static(0));
	putnl();

	putnum(read_later());
	putnl();
	putnum(read_static_global());
	putnl();

	v = seed;
	seed++;
	putnum(v);
	putsp();
	putnum(seed);
	putnl();

	return 0;
}
