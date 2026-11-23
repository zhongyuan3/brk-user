#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Aggregate parameters and return values by value.
 * Expected output:
 *   7
 *   10
 *   7 10
 *   10 13
 *   10 14
 *   8 11
 */

struct pair {
	int a;
	int b;
};

struct big {
	long v[4];
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

struct pair make_pair(int a, int b)
{
	struct pair p;

	p.a = a;
	p.b = b;
	return p;
}

int pair_sum(struct pair p)
{
	return p.a + p.b;
}

struct pair bump(struct pair p)
{
	p.a++;
	p.b += 2;
	return p;
}

struct pair combine(struct pair x, struct pair y)
{
	struct pair r;

	r.a = x.a + y.a;
	r.b = x.b + y.b;
	return r;
}

struct big make_big(long base)
{
	struct big b;

	b.v[0] = base;
	b.v[1] = base + 1;
	b.v[2] = base + 2;
	b.v[3] = base + 3;
	return b;
}

long big_sum(struct big b)
{
	return b.v[0] + b.v[1] + b.v[2] + b.v[3];
}

int main(void)
{
	struct pair p;
	struct pair q;
	struct pair r;
	struct big b;

	p = make_pair(3, 4);
	putnum(pair_sum(p));
	putchar('\n');

	q = bump(p);
	putnum(pair_sum(q));
	putchar('\n');

	r = combine(p, q);
	putnum(r.a);
	putchar(' ');
	putnum(r.b);
	putchar('\n');

	b = make_big(10);
	putnum((int)b.v[0]);
	putchar(' ');
	putnum((int)b.v[3]);
	putchar('\n');

	putnum(pair_sum(make_pair(4, 6)));
	putchar(' ');
	putnum((int)big_sum(make_big(2)));
	putchar('\n');

	p = make_pair(1, 2);
	q = make_pair(6, 7);
	r = combine(bump(p), q);
	putnum(r.a);
	putchar(' ');
	putnum(r.b);
	putchar('\n');

	return 0;
}
