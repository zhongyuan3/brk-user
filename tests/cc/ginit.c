#include <cc.h>

struct P {
	int x;
	int y;
	char tag[8];
};

union U {
	int i;
	char c[4];
};

int gadd(int a, int b)
{
	return a + b;
}

struct P gp = { 7, 8, "ab" };
struct P ga[2] = { { 1, 2, "a" }, { 3, 4, "b" } };
int gnums[] = { 10, 20, 30 };
int gmat[2][2] = { { 1, 2 }, { 3, 4 } };
char *gstr = "global";
char *gstrs[] = { "x", "y" };
int gx = 5;
int *gpx = &gx;
union U gu = { 0x41424344 };
int (*gfp)(int, int) = gadd;

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
	putnum(gp.x + gp.y);
	putchar(' ');
	puts(gp.tag);

	putnum(ga[0].x + ga[1].y);
	putchar(' ');
	puts(ga[1].tag);

	putnum(gnums[0] + gnums[1] + gnums[2]);
	putchar('\n');

	putnum(gmat[0][0] + gmat[0][1] + gmat[1][0] + gmat[1][1]);
	putchar('\n');

	puts(gstr);
	puts(gstrs[0]);
	puts(gstrs[1]);

	putnum(*gpx);
	putchar('\n');

	putchar(gu.c[0]);
	putchar(gu.c[1]);
	putchar(gu.c[2]);
	putchar(gu.c[3]);
	putchar('\n');

	putnum(gfp(2, 3));
	putchar('\n');
	return 0;
}
