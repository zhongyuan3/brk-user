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
	int a[1000];
	int i;

	for (i = 0; i < 1000; i++)
		a[i] = i;
	putnum(a[999]);
	putchar('\n');
	putnum(a[512]);
	putchar('\n');
	return 0;
}
