#include <cc.h>

struct P {
	int x;
	int y;
	char tag[4];
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
	int a[5] = { 1, 2, 3 };
	char s[] = "hi";
	int b[] = { 4, 5, 6 };
	struct P p = { 7, 8, "ab" };
	struct P q = { 9 };
	int m[2][2] = { { 1, 2 }, { 3, 4 } };
	char two[2] = "hi";
	int x = 3;
	int r[2] = { x, x + 1 };
	struct P base = { 10, 20, "xy" };
	struct P copy;

	copy = base;

	putnum(a[0] + a[1] + a[2] + a[3] + a[4]);
	putchar('\n');
	puts(s);
	putnum(b[0] + b[1] + b[2]);
	putchar('\n');
	putnum(p.x + p.y);
	putchar(' ');
	puts(p.tag);
	putnum(q.x + q.y + q.tag[0]);
	putchar('\n');
	putnum(m[0][0] + m[0][1] + m[1][0] + m[1][1]);
	putchar('\n');
	putchar(two[0]);
	putchar(two[1]);
	putchar('\n');
	putnum(r[0] + r[1]);
	putchar('\n');
	putnum(copy.x + copy.y);
	putchar(' ');
	puts(copy.tag);

	return 0;
}
