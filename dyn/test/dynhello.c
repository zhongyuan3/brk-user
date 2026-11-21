/*
 * Dynamic smoke test: PT_INTERP=/lib/ld-brk.so, DT_NEEDED=libhello.so.
 * Freestanding: raw syscalls, weak references resolved by the linker
 * at load time (undefined without it -> clean error path).
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

extern int hello_value(void) __attribute__((weak));
extern const char *hello_msg(void) __attribute__((weak));
extern int hello_inited __attribute__((weak));

static ulong slen(const char *s)
{
	ulong n = 0;

	while (s[n])
		++n;
	return n;
}

int main(void)
{
	if (!hello_value || !hello_msg || !&hello_inited) {
		__sys3(1, 2, (long)"dynhello: symbols unresolved\n", 28);
		return 1;
	}
	__sys3(1, 1, (long)hello_msg(), (long)slen(hello_msg()));
	if (hello_value() != 42)
		return 2;
	if (!hello_inited)
		return 3;
	return 0;
}

void _start(void)
{
	register long a0 asm("a0");
	long st = main();

	(void)a0;
	__sys3(42, st, 0, 0);
	__builtin_unreachable();
}
