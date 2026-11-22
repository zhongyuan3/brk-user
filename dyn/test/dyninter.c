/*
 * Symbol interposition proof: this executable defines its own puts(),
 * exported through --export-dynamic, so libdyn's announce() - and any
 * other caller in the process - resolves to this version first.
 * printf comes from libc.so alongside the override, untouched.
 */
typedef unsigned long ulong;

static long wr(const char *s, ulong n)
{
	register long a0 asm("a0") = 1;
	register long a1 asm("a1") = (long)s;
	register long a2 asm("a2") = (long)n;
	register long a7 asm("a7") = 1;

	asm volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
	return a0;
}

int puts(const char *s)
{
	const char *tag = "[interposed] ";
	ulong n = 0, i;

	wr(tag, 13);
	while (s[n])
		++n;
	/* strip the DSO's newline, print once with ours */
	for (i = n; i > 0 && s[i - 1] == '\n'; --i)
		;
	wr(s, i);
	wr("\n", 1);
	return 0;
}

void announce(void);

int main(void)
{
	announce();
	return 0;
}
