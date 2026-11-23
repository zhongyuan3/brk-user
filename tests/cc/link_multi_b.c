/*
 * Second unit of the multi-object linking test; see link_multi_a.c.
 * Exercises external data defined in another unit, cross-unit data
 * relocations, bss zero fill, symbol alignment, function calls and
 * unit-local static symbols with colliding names.
 */

extern int link_shared;
extern int link_bss[4];
extern long long link_aligned;
extern int *link_cross_ptr;
extern char *link_cross_str;
extern int link_add(int a, int b);
extern int link_call_helper_a(void);
extern int link_local_a(void);
extern int link_tag_a(void);

int link_from_b = 17;
char link_msg_b[8] = "cross";

static int local_value = 20;
static char local_tag[] = "beta";

static int shared_helper(void)
{
	return 2;
}

int link_local_b(void)
{
	return local_value;
}

int link_tag_b(void)
{
	return local_tag[0];
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

int main(void)
{
	puts("link");
	putnum(link_shared + 1);
	putchar('\n');
	putnum(link_add(2, 3));
	putchar('\n');
	putnum(link_call_helper_a());
	putchar('\n');
	putnum(shared_helper());
	putchar('\n');
	putnum(link_local_a());
	putchar('\n');
	putnum(link_local_b());
	putchar('\n');
	putchar(link_tag_a());
	putchar(link_tag_b());
	putchar('\n');
	putnum(*link_cross_ptr);
	putchar('\n');
	putnum(link_bss[0] + link_bss[1] + link_bss[2] + link_bss[3]);
	putchar('\n');
	putnum((int)((unsigned long)&link_aligned & 7));
	putchar('\n');
	puts(link_cross_str);
	return 0;
}
