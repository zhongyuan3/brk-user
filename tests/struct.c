#include <cc.h>

struct Point {
	int x;
	int y;
};

typedef struct Point Point;

typedef struct {
	char name[8];
	int value;
} Entry;

union U {
	int i;
	char c[4];
};

enum Color { RED, GREEN = 5, BLUE };

struct Node {
	int value;
	struct Node *next;
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
	struct Point p;
	Point q;
	struct Point *pp;
	struct Point arr[2];
	Entry e;
	union U u;
	struct Node n;
	struct Node *node;

	p.x = 3;
	p.y = 4;
	q = p;
	putnum(q.x + q.y);
	putchar('\n');

	pp = &q;
	pp->x = 10;
	putnum(q.x);
	putchar('\n');

	arr[0].x = 1;
	arr[0].y = 2;
	arr[1] = arr[0];
	arr[1].y = 9;
	putnum(arr[1].x + arr[1].y);
	putchar('\n');

	e.value = 42;
	e.name[0] = 'A';
	e.name[1] = 0;
	puts(e.name);
	putnum(e.value);
	putchar('\n');

	u.i = 0x41424344;
	putchar(u.c[0]);
	putchar(u.c[1]);
	putchar(u.c[2]);
	putchar(u.c[3]);
	putchar('\n');

	putnum(RED);
	putchar(' ');
	putnum(GREEN);
	putchar(' ');
	putnum(BLUE);
	putchar('\n');

	putnum(sizeof(struct Point));
	putchar(' ');
	putnum(sizeof(union U));
	putchar(' ');
	putnum(sizeof(Entry));
	putchar('\n');

	n.value = 7;
	node = &n;
	putnum(node->value);
	putchar(' ');
	putnum(node == 0);
	putchar('\n');

	return 0;
}
