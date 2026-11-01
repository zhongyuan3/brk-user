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

long big = 5000000000;
long neg = -5000000000;

int main(void)
{
	long x;
	unsigned long u;

	x = big + 1000000000;
	putnum(x == 6000000000);
	putnum(neg < 0);
	u = 0xffffffffffffffff;
	putnum(u > 0);
	putnum(sizeof(long));
	putchar('\n');
	return 0;
}
