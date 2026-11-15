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

int main(int argc, char **argv)
{
	int fd, n;
	char buf[32];
	int i;
	unsigned int u;

	fd = open("/tmp/io.txt", 577, 0644);
	n = write(fd, "file-io\n", 8);
	close(fd);
	putnum(n);
	putchar('\n');

	fd = open("/tmp/io.txt", 0);
	n = read(fd, buf, 31);
	close(fd);
	buf[n] = 0;
	puts(buf);

	putnum(1 << 3);
	putchar(' ');
	putnum(16 >> 2);
	putchar(' ');
	putnum(-16 >> 2);
	putchar('\n');

	u = 0xffffffff;
	putnum(u > 0);
	putchar(' ');
	putnum((char)300);
	putchar(' ');
	putnum((unsigned char)-1);
	putchar(' ');
	putnum((short)70000);
	putchar('\n');

	putnum((1 && 2) + (0 || 3) + (0 ? 5 : 7));
	putchar('\n');

	i = 10;
	i *= 3;
	i -= 5;
	i /= 5;
	i %= 4;
	putnum(i);
	putchar(' ');
	i = 1;
	i <<= 4;
	i >>= 2;
	putnum(i);
	putchar(' ');
	i = 7;
	i &= 3;
	i |= 8;
	i ^= 1;
	putnum(i);
	putchar('\n');

	putnum(-5 / 3);
	putchar(' ');
	putnum(-5 % 3);
	putchar('\n');

	putnum(sizeof(int *));
	putchar(' ');
	putnum(sizeof("abc"));
	putchar(' ');
	putnum('A');
	putchar(' ');
	putnum('\n');
	putchar(' ');
	putnum(0x10 + 010 + 10);
	putchar('\n');

	putnum((1, 2, 3));
	putchar(' ');
	putnum(!0 + !5);
	putchar(' ');
	putnum(~0);
	putchar('\n');

	putnum(argc);
	putchar(' ');
	puts(argv[0]);
	if (argc > 1)
		puts(argv[1]);

	return 0;
}
