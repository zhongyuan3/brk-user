#include <cc.h>

/*
 * Expected output:
 *   8
 *   0 15 6 74565
 *   25859
 *   -3 12 1
 *   8
 *   4 4
 *   6 31 2
 *   120 121 4
 */

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

void putsp(void)
{
	putchar(' ');
}

void putnl(void)
{
	putchar('\n');
}

struct Bits {
	unsigned int a : 3;
	unsigned int b : 5;
	unsigned int c : 4;
	unsigned int d : 4;
	unsigned int : 0;
	unsigned int e : 2;
};

struct Signed {
	int a : 4;
	unsigned int b : 4;
	int c : 2;
};

struct Padded {
	unsigned int a : 4;
	unsigned int : 4;
	unsigned int b : 4;
	unsigned int c : 4;
};

struct ZeroAlign {
	char c;
	int : 0;
	char d;
};

union Pack {
	unsigned int word;
	struct {
		unsigned int a : 3;
		unsigned int b : 5;
		unsigned int c : 4;
		unsigned int d : 20;
	} bits;
};

union PaddedPack {
	unsigned int word;
	struct Padded p;
};

struct Bits gbits = { 5, 17, 3, 4, 1 };

int main(void)
{
	struct Bits bits;
	struct Signed s;
	struct ZeroAlign z;
	union Pack pack;
	union PaddedPack pp;

	putnum((int)sizeof(struct Bits));
	putnl();

	pack.word = 0x12345678;
	putnum((int)pack.bits.a);
	putsp();
	putnum((int)pack.bits.b);
	putsp();
	putnum((int)pack.bits.c);
	putsp();
	putnum((int)pack.bits.d);
	putnl();

	pp.word = 0;
	pp.p.a = 3;
	pp.p.b = 5;
	pp.p.c = 6;
	putnum((int)pp.word);
	putnl();

	s.a = -3;
	s.b = 12;
	s.c = 1;
	putnum(s.a);
	putsp();
	putnum((int)s.b);
	putsp();
	putnum(s.c);
	putnl();

	putnum((int)sizeof(struct ZeroAlign));
	putnl();

	putnum((int)sizeof(struct Signed));
	putsp();
	putnum((int)sizeof(struct Padded));
	putnl();

	bits = gbits;
	bits.a += 1;
	bits.b = 31;
	bits.e++;
	putnum((int)bits.a);
	putsp();
	putnum((int)bits.b);
	putsp();
	putnum((int)bits.e);
	putnl();

	z.c = 'x';
	z.d = 'y';
	putnum((int)z.c);
	putsp();
	putnum((int)z.d);
	putsp();
	putnum((int)((char *)&z.d - (char *)&z));
	putnl();

	return 0;
}
