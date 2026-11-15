char *greet = "hi";
char text[16] = "abc";
int nums[4] = { 3, 1, 4, 2 };

int add(int a, int b)
{
	return a + b;
}

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

int fib(int n)
{
	if (n < 2)
		return n;
	return fib(n - 1) + fib(n - 2);
}

int main(int argc, char **argv)
{
	int i, s;
	int a[4];
	int *p;

	for (i = 0; i < 4; i++)
		a[i] = nums[i] * 2;
	s = 0;
	for (i = 0; i < 4; i++)
		s += a[i];
	putnum(s);
	putchar('\n');

	i = 0;
	s = 0;
	while (1) {
		i++;
		if (i == 2)
			continue;
		if (i > 5)
			break;
		s += i;
	}
	putnum(s);
	putchar('\n');

	i = 0;
	do {
		i++;
	} while (i < 3);
	putnum(i);
	putchar('\n');

	p = a;
	putnum(*(p + 2));
	putchar('\n');
	putnum(p[3]);
	putchar('\n');
	p++;
	putnum(*p);
	putchar('\n');

	i = 5;
	putnum(i++);
	putchar(' ');
	putnum(++i);
	putchar('\n');

	puts(greet);
	puts(text);

	putnum(sizeof(int));
	putchar(' ');
	putnum(sizeof(a));
	putchar(' ');
	putnum(sizeof(char));
	putchar('\n');

	putnum(fib(10));
	putchar('\n');

	putnum(add(100, 23));
	putchar('\n');

	putnum(argc);
	putchar('\n');

	return 0;
}
