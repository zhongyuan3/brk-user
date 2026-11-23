#include <cc.h>

/*
 * Expected output:
 *   13 24
 *   3 103
 *   170
 *   13 24
 *   8 16 4
 *   1
 *   3 6
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

struct Pair {
	int a;
	int b;
};

struct Big {
	long x;
	int y;
	char z;
};

union Value {
	int i;
	char c[4];
};

struct Pair make_pair(int a, int b)
{
	struct Pair p;

	p.a = a;
	p.b = b;
	return p;
}

struct Pair add_pair(struct Pair p, struct Pair q)
{
	struct Pair r;

	r.a = p.a + q.a;
	r.b = p.b + q.b;
	return r;
}

struct Pair bump(struct Pair p)
{
	p.a += 100;
	return p;
}

struct Big make_big(long x, int y, char z)
{
	struct Big b;

	b.x = x;
	b.y = y;
	b.z = z;
	return b;
}

long sum_big(struct Big b)
{
	return b.x + b.y + b.z;
}

int bump_union(union Value v)
{
	v.i += 1;
	return v.i;
}

struct Pair (*pick(int which))(struct Pair, struct Pair)
{
	(void)which;
	return add_pair;
}

int main(void)
{
	struct Pair a;
	struct Pair b;
	struct Pair c;
	struct Pair arr[2];
	struct Big big;
	union Value v;

	a = make_pair(3, 4);
	b = make_pair(10, 20);
	c = add_pair(a, b);
	putnum(c.a);
	putsp();
	putnum(c.b);
	putnl();

	b = bump(a);
	putnum(a.a);
	putsp();
	putnum(b.a);
	putnl();

	big = make_big(100, 5, 'A');
	putnum((int)sum_big(big));
	putnl();

	c = pick(0)(a, make_pair(10, 20));
	putnum(c.a);
	putsp();
	putnum(c.b);
	putnl();

	putnum((int)sizeof(struct Pair));
	putsp();
	putnum((int)sizeof(struct Big));
	putsp();
	putnum((int)sizeof(union Value));
	putnl();

	v.i = 0x41424344;
	bump_union(v);
	putnum(v.i == 0x41424344);
	putnl();

	arr[0] = a;
	arr[1] = arr[0];
	arr[1].b = 6;
	putnum(arr[1].a);
	putsp();
	putnum(arr[1].b);
	putnl();

	return 0;
}
