#include <cc.h>

struct P {
	int x;
	int y;
	char tag[8];
};

struct Outer {
	struct P p;
	int z;
};

struct P gp = { .y = 8, .x = 7, .tag = "ab" };
struct Outer go = { 1, 2, "cd", 3 };
int ga[5] = { [2] = 20, 30, [0] = 10 };
int gmat[2][2] = { 1, 2, 3, 4 };
struct P gps[2] = { 1, 2, "a", 3, 4, "b" };

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
	struct P p = { .x = 5, .tag = "lo" };
	int a[4] = { [3] = 9, [1] = 2 };
	int m[2][2] = { 5, 6, 7, 8 };
	struct P arr[2] = { 9, 10, "x", 11, 12, "y" };

	putnum(gp.x);
	putchar(' ');
	putnum(gp.y);
	putchar(' ');
	puts(gp.tag);

	putnum(go.p.x);
	putchar(' ');
	putnum(go.p.y);
	putchar(' ');
	puts(go.p.tag);
	putnum(go.z);
	putchar('\n');

	putnum(ga[0]);
	putchar(' ');
	putnum(ga[1]);
	putchar(' ');
	putnum(ga[2]);
	putchar(' ');
	putnum(ga[3]);
	putchar(' ');
	putnum(ga[4]);
	putchar('\n');

	putnum(gmat[0][0]);
	putchar(' ');
	putnum(gmat[0][1]);
	putchar(' ');
	putnum(gmat[1][0]);
	putchar(' ');
	putnum(gmat[1][1]);
	putchar('\n');

	putnum(gps[0].x);
	putchar(' ');
	puts(gps[0].tag);
	putnum(gps[1].x);
	putchar(' ');
	puts(gps[1].tag);

	putnum(p.x);
	putchar(' ');
	putnum(p.y);
	putchar(' ');
	puts(p.tag);

	putnum(a[0]);
	putchar(' ');
	putnum(a[1]);
	putchar(' ');
	putnum(a[2]);
	putchar(' ');
	putnum(a[3]);
	putchar('\n');

	putnum(m[0][0]);
	putchar(' ');
	putnum(m[0][1]);
	putchar(' ');
	putnum(m[1][0]);
	putchar(' ');
	putnum(m[1][1]);
	putchar('\n');

	putnum(arr[0].x);
	putchar(' ');
	puts(arr[0].tag);
	putnum(arr[1].x);
	putchar(' ');
	puts(arr[1].tag);

	return 0;
}
