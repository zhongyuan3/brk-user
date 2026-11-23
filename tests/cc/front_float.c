#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Expected output:
 *   1 -2 -6 2 -1 4 1
 *   -1 6 1 4 8 8
 *   -2
 *   6
 *   5
 *   3 9
 *   0 0 1 1 7
 *   63
 *   3 6
 *   3 2
 *   3 11
 *   2
 *   3
 *   125
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

float gf = 1.5f;
double gd = -2e3;
float garr[3] = { 0.5f, -1.5f, 2.25f };
double gdarr[2] = { 1e2, -1e-1 };

double dscale(double x, float y)
{
	return x * y;
}

float fscale(float x, double y)
{
	return x * y;
}

int f2i(float x)
{
	return (int)x;
}

int main(void)
{
	float f;
	double d;
	int i;
	int n;

	putnum((int)gf);
	putsp();
	putnum((int)(gd / 1000));
	putsp();
	putnum((int)(garr[1] * 4));
	putsp();
	putnum((int)(gdarr[0] / 50));
	putsp();
	putnum((int)(gdarr[1] * 10));
	putsp();
	putnum((int)(garr[2] * 2));
	putsp();
	putnum((int)(gd / gd));
	putnl();

	f = 1.5f;
	d = -2.5;
	putnum((int)(d + f));
	putsp();
	putnum((int)(f * 4.0));
	putsp();
	putnum(f2i(f));
	putsp();
	putnum((int)sizeof(float));
	putsp();
	putnum((int)sizeof(double));
	putsp();
	putnum((int)sizeof(long double));
	putnl();

	i = d;
	putnum(i);
	putnl();

	f = 1.5f;
	f += 1;
	f *= 2;
	f -= 0.5f;
	f /= 1.5f;
	putnum((int)(f * 2));
	putnl();

	d = 10.0;
	d /= 4;
	putnum((int)(d * 2));
	putnl();

	f = 1.5f;
	n = 0;
	while (f < 4) {
		f += 1;
		n++;
	}
	putnum(n);
	putsp();
	putnum((int)(f * 2));
	putnl();

	f = 0;
	if (f)
		n = 1;
	else
		n = 0;
	putnum(n);
	putsp();
	f = -3.5f;
	putnum(!f);
	putsp();
	putnum(f && 2);
	putsp();
	putnum(0.0 || f);
	putsp();
	putnum(f ? 7 : 8);
	putnl();

	f = 1.0f;
	n = (f > 0) + (f >= 1.0) * 2 + (f < 2.0) * 4 +
	    (f <= 1.0) * 8 + (f == 1.0) * 16 + (f != 2.0) * 32;
	putnum(n);
	putnl();

	f = 2.5f;
	d = 1.25;
	putnum((int)dscale(d, f));
	putsp();
	putnum((int)(fscale(f, d) * 2));
	putnl();

	f = 5.0f;
	i = 0;
	do {
		f -= 1.0f;
		i++;
	} while (f > 2.0);
	putnum(i);
	putsp();
	putnum((int)f);
	putnl();

	f = 1.0f;
	for (n = 0; f < 5.0; n++)
		f += 1.5f;
	putnum(n);
	putsp();
	putnum((int)(f * 2));
	putnl();

	f = 1.5f;
	putnum((int)(f + 0.5f));
	putnl();

	f++;
	++f;
	putnum((int)f);
	putnl();

	d = 1e3;
	putnum((int)(d / 8));
	putnl();

	return 0;
}
