#include <cc.h>

#define ADD(a, b) ((a) + (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define SQUARE(x) ((x) * (x))
#define TWICE(x) (x + x)
#define ID(x) x
#define PRINTF(...) puts(__VA_ARGS__)
#define FIRST(a, ...) a
#define REST(a, ...) (__VA_ARGS__)
#define ARGS(a, b) a, b

int sum3(int a, int b, int c)
{
	return a + b + c;
}

int gv = ADD(1, 4);

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
	putnum(ADD(2, 3));
	putchar('\n');
	putnum(MAX(4, 9));
	putchar('\n');
	putnum(SQUARE(3 + 1));
	putchar('\n');
	putnum(TWICE(5));
	putchar('\n');
	putnum(ID(7));
	putchar('\n');
	putnum(ID(ID(9)));
	putchar('\n');
	putnum(FIRST(11, 22, 33));
	putchar('\n');
	putnum(REST(0, 4 + 5));
	putchar('\n');
	putnum(sum3(ARGS(1, 2), 3));
	putchar('\n');
	putnum(gv);
	putchar('\n');
	PRINTF("macros");
	return 0;
}
