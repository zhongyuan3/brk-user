#include <cc.h>

int add(int a, int b)
{
	return a + b;
}

int sub(int a, int b)
{
	return a - b;
}

typedef int (*binop)(int, int);

int apply(binop f, int a, int b)
{
	return f(a, b);
}

int (*pick(int which))(int, int)
{
	if (which)
		return add;
	return sub;
}

struct Ops {
	int (*fn)(int, int);
	int unused;
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
	int (*fp)(int, int);
	binop f2;
	struct Ops ops;
	int (*table[2])(int, int);
	int i;

	fp = add;
	putnum(fp(10, 5));
	putchar('\n');

	fp = &sub;
	putnum(fp(10, 5));
	putchar('\n');

	f2 = add;
	putnum(apply(f2, 100, 23));
	putchar('\n');

	putnum(pick(1)(7, 8));
	putchar('\n');
	putnum(pick(0)(7, 8));
	putchar('\n');

	table[0] = add;
	table[1] = sub;
	for (i = 0; i < 2; i++) {
		putnum(table[i](20, 3));
		putchar(' ');
	}
	putchar('\n');

	ops.fn = add;
	putnum(ops.fn(1, 2));
	putchar('\n');

	return 0;
}
