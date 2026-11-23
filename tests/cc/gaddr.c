/*
 * Constant address initializers beyond simple &global / string forms.
 * Expected output:
 *   20 30 30 6
 */
#include <cc.h>

int arr[4] = { 10, 20, 30, 40 };
int *p1 = &arr[1];
int *p2 = arr + 2;
int *p3 = &arr[3] - 1;

struct S {
	int x;
	int y;
};

struct S gs = { 5, 6 };
int *p4 = &gs.y;

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
	putnum(*p1);
	putchar(' ');
	putnum(*p2);
	putchar(' ');
	putnum(*p3);
	putchar(' ');
	putnum(*p4);
	putchar('\n');
	return 0;
}
