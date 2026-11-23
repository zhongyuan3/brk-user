#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Bit-field loads, stores, compound assignment and increments.
 * Expected output:
 *   5 17 -3 1
 *   7 0 1 -8 7
 *   0
 *   0
 *   123 9 456
 *   1 579
 *   7 -3 1 -6
 *   18 703710 155
 */

struct bits {
	unsigned int a : 3;
	unsigned int b : 5;
	int c : 4;
	unsigned int d : 1;
};

struct chars {
	unsigned char x : 3;
	signed char y : 4;
};

struct mixed {
	int p;
	unsigned int f : 4;
	int q;
};

struct wide {
	unsigned long lo : 40;
	unsigned long hi : 20;
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

int main(void)
{
	struct bits v;
	struct chars ch;
	struct mixed m;
	struct wide w;

	v.a = 5;
	v.b = 17;
	v.c = -3;
	v.d = 1;
	putnum(v.a);
	putchar(' ');
	putnum(v.b);
	putchar(' ');
	putnum(v.c);
	putchar(' ');
	putnum(v.d);
	putchar('\n');

	v.a += 2;
	putnum(v.a);
	putchar(' ');
	v.a++;
	putnum(v.a);
	putchar(' ');
	++v.a;
	putnum(v.a);
	putchar(' ');
	v.c -= 5;
	putnum(v.c);
	putchar(' ');
	v.c--;
	putnum(v.c);
	putchar('\n');

	v.b = 31;
	v.b++;
	putnum(v.b);
	putchar('\n');

	v.d = 2;
	putnum(v.d);
	putchar('\n');

	m.p = 123;
	m.f = 9;
	m.q = 456;
	putnum(m.p);
	putchar(' ');
	putnum(m.f);
	putchar(' ');
	putnum(m.q);
	putchar('\n');

	m.f += 8;
	putnum(m.f);
	putchar(' ');
	putnum(m.p + m.q);
	putchar('\n');

	ch.x = 7;
	ch.y = -3;
	putnum(ch.x);
	putchar(' ');
	putnum(ch.y);
	putchar(' ');
	ch.x += 2;
	putnum(ch.x);
	putchar(' ');
	ch.y *= 2;
	putnum(ch.y);
	putchar('\n');

	w.lo = 0x123456789a;
	w.hi = 0xabcde;
	putnum((int)(w.lo >> 32));
	putchar(' ');
	putnum((int)w.hi);
	putchar(' ');
	w.lo += 1;
	putnum((int)(w.lo & 0xff));
	putchar('\n');

	return 0;
}
