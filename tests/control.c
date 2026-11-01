#include <cc.h>

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

int classify(int x)
{
	switch (x) {
	case 0:
		return 10;
	case 1:
	case 2:
		return 20;
	case 3: {
		int y = x * 2;

		return y;
	}
	default:
		return 99;
	}
}

int fall(int x)
{
	int r = 0;

	switch (x) {
	case 1:
		r += 1;
	case 2:
		r += 2;
		break;
	case 3:
		r += 3;
	default:
		r += 10;
	}
	return r;
}

int loop_switch(int n)
{
	int i;
	int s = 0;

	for (i = 0; i < n; i++) {
		switch (i) {
		case 1:
			continue;
		case 3:
			break;
		default:
			s += i;
		}
		s += 100;
	}
	return s;
}

int goto_test(int n)
{
	int s = 0;
	int i = 0;

again:
	if (i < n) {
		s += i;
		i++;
		goto again;
	}
	goto done;
	s = 999;
done:
	return s;
}

int main(void)
{
	putnum(classify(0));
	putchar(' ');
	putnum(classify(1));
	putchar(' ');
	putnum(classify(2));
	putchar(' ');
	putnum(classify(3));
	putchar(' ');
	putnum(classify(7));
	putchar('\n');

	putnum(fall(1));
	putchar(' ');
	putnum(fall(2));
	putchar(' ');
	putnum(fall(3));
	putchar(' ');
	putnum(fall(9));
	putchar('\n');

	putnum(loop_switch(5));
	putchar('\n');

	putnum(goto_test(5));
	putchar('\n');

	return 0;
}
