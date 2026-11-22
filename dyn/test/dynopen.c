/*
 * dlopen smoke test: weak references to the dl* API (resolved by
 * /lib/ld-brk.so when this binary runs interpreted), a round trip
 * through open/lookup/close/reopen, and the failure paths.
 */
typedef unsigned long ulong;

static long __sys3(long n, long a, long b, long c)
{
	register long a0 asm("a0") = a;
	register long a1 asm("a1") = b;
	register long a2 asm("a2") = c;
	register long a7 asm("a7") = n;

	asm volatile("ecall"
		     : "+r"(a0)
		     : "r"(a1), "r"(a2), "r"(a7)
		     : "memory");
	return a0;
}

static void wr(const char *s, ulong n)
{
	__sys3(1, 1, (long)s, (long)n);
}

static ulong slen(const char *s)
{
	ulong n = 0;

	while (s[n])
		++n;
	return n;
}

#define RTLD_LAZY 1
#define RTLD_NOW 2

extern void *dlopen(const char *, int) __attribute__((weak));
extern void *dlsym(void *, const char *) __attribute__((weak));
extern int dlclose(void *) __attribute__((weak));
extern char *dlerror(void) __attribute__((weak));

int base_value(void)
{
	return 3;
}

typedef int (*sum_fn)(void);

int main(void)
{
	void *h, *h2;
	sum_fn sum;
	int *flag;

	if (!dlopen || !dlsym || !dlclose || !dlerror) {
		wr("dynopen: no dl API\n", 20);
		return 1;
	}

	h = dlopen("/lib/libplug.so", RTLD_NOW);
	if (!h) {
		char *e;

		wr("dynopen: dlopen failed: ", 24);
		e = dlerror();
		if (e)
			wr(e, slen(e));
		wr("\n", 1);
		return 2;
	}

	sum = (sum_fn)dlsym(h, "plug_sum");
	if (!sum)
		return 3;
	if (sum() != 3 + 7)
		return 4;

	flag = (int *)dlsym(h, "plug_inited");
	if (!flag || *flag != 1)
		return 5;

	if (dlclose(h) != 0)
		return 6;

	/* the handle is gone: further lookups must fail cleanly */
	if (dlsym(h, "plug_sum"))
		return 7;

	/* round trip: unload ran the destructor, reload runs the init */
	h2 = dlopen("/lib/libplug.so", RTLD_NOW);
	if (!h2)
		return 8;
	flag = (int *)dlsym(h2, "plug_inited");
	if (!flag || *flag != 1)
		return 9;
	if (dlclose(h2) != 0)
		return 10;

	(void)dlerror();
	if (dlopen("/lib/libnope.so", RTLD_NOW) || !dlerror())
		return 11;

	wr("dlopen ok\n", 10);
	return 0;
}

void _start(void)
{
	long st = main();

	__sys3(42, st, 0, 0);
	__builtin_unreachable();
}
