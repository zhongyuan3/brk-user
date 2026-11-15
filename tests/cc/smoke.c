char msg[] = "sum=";
int total;

int add(int a, int b)
{
	return a + b;
}

void print_num(int n)
{
	if (n < 0) {
		putchar('-');
		n = -n;
	}
	if (n >= 10)
		print_num(n / 10);
	putchar('0' + n % 10);
}

int main(void)
{
	int i;
	int a[5];

	for (i = 0; i < 5; i++)
		a[i] = i * i;
	for (i = 0; i < 5; i++)
		total = add(total, a[i]);
	puts(msg);
	print_num(total);
	putchar('\n');
	return 0;
}
