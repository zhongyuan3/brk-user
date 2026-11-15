#include <cc.h>

extern int shared;
int add(int a, int b);
int get_shared(void);
int a_helper(void);

static int helper(void)
{
	return 2;
}

struct Pair {
	int a;
	int b;
};

typedef struct Pair Pair;

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
	Pair p = { 3, 4 };
	Pair q;

	q = p;
	shared = shared + 1;

	puts("multi");
	putnum(add(2, 3));
	putchar('\n');
	putnum(get_shared());
	putchar('\n');
	putnum(a_helper());
	putchar('\n');
	putnum(helper());
	putchar('\n');
	putnum(q.a + q.b);
	putchar('\n');
	return 0;
}
