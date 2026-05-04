#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#include <brk/syscall.h>
#include <errno.h>

#define __SYSCALL_LL_E(x) (x)
#define __SYSCALL_LL_O(x) (x)

#define __asm_syscall(...)                                                     \
	__asm__ __volatile__("ecall\n\t" : "=r"(a0) : __VA_ARGS__ : "memory"); \
	return a0;

static inline long long __syscall0(long long n)
{
	register long long a7 __asm__("a7") = n;
	register long long a0 __asm__("a0");
	__asm_syscall("r"(a7))
}

static inline long long __syscall1(long long n, long long a)
{
	register long long a7 __asm__("a7") = n;
	register long long a0 __asm__("a0") = a;
	__asm_syscall("r"(a7), "0"(a0))
}

static inline long long __syscall2(long long n, long long a, long long b)
{
	register long long a7 __asm__("a7") = n;
	register long long a0 __asm__("a0") = a;
	register long long a1 __asm__("a1") = b;
	__asm_syscall("r"(a7), "0"(a0), "r"(a1))
}

static inline long long __syscall3(long long n, long long a, long long b,
				   long long c)
{
	register long long a7 __asm__("a7") = n;
	register long long a0 __asm__("a0") = a;
	register long long a1 __asm__("a1") = b;
	register long long a2 __asm__("a2") = c;
	__asm_syscall("r"(a7), "0"(a0), "r"(a1), "r"(a2))
}

static inline long long __syscall4(long long n, long long a, long long b,
				   long long c, long long d)
{
	register long long a7 __asm__("a7") = n;
	register long long a0 __asm__("a0") = a;
	register long long a1 __asm__("a1") = b;
	register long long a2 __asm__("a2") = c;
	register long long a3 __asm__("a3") = d;
	__asm_syscall("r"(a7), "0"(a0), "r"(a1), "r"(a2), "r"(a3))
}

static inline long long __syscall5(long long n, long long a, long long b,
				   long long c, long long d, long long e)
{
	register long long a7 __asm__("a7") = n;
	register long long a0 __asm__("a0") = a;
	register long long a1 __asm__("a1") = b;
	register long long a2 __asm__("a2") = c;
	register long long a3 __asm__("a3") = d;
	register long long a4 __asm__("a4") = e;
	__asm_syscall("r"(a7), "0"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4))
}

static inline long long __syscall6(long long n, long long a, long long b,
				   long long c, long long d, long long e,
				   long long f)
{
	register long long a7 __asm__("a7") = n;
	register long long a0 __asm__("a0") = a;
	register long long a1 __asm__("a1") = b;
	register long long a2 __asm__("a2") = c;
	register long long a3 __asm__("a3") = d;
	register long long a4 __asm__("a4") = e;
	register long long a5 __asm__("a5") = f;
	__asm_syscall("r"(a7), "0"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4),
		      "r"(a5))
}

#define __scc(X) ((long long)(X))

#define __syscall1(n, a) __syscall1(n, __scc(a))
#define __syscall2(n, a, b) __syscall2(n, __scc(a), __scc(b))
#define __syscall3(n, a, b, c) __syscall3(n, __scc(a), __scc(b), __scc(c))
#define __syscall4(n, a, b, c, d) \
	__syscall4(n, __scc(a), __scc(b), __scc(c), __scc(d))
#define __syscall5(n, a, b, c, d, e) \
	__syscall5(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e))
#define __syscall6(n, a, b, c, d, e, f)                                 \
	__syscall6(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e), \
		   __scc(f))

#define __SYSCALL_NARGS_X(a, b, c, d, e, f, g, h, n, ...) n
#define __SYSCALL_NARGS(...) \
	__SYSCALL_NARGS_X(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0, )
#define __SYSCALL_CONCAT_X(a, b) a##b
#define __SYSCALL_CONCAT(a, b) __SYSCALL_CONCAT_X(a, b)
#define __SYSCALL_DISP(b, ...)                            \
	__SYSCALL_CONCAT(b, __SYSCALL_NARGS(__VA_ARGS__)) \
	(__VA_ARGS__)

#define __syscall(...) __SYSCALL_DISP(__syscall, __VA_ARGS__)

static inline long long __syscall_ret(long long err)
{
	if (err < 0) {
		errno = -err;
		return -1;
	}
	return err;
}

#define syscall(...) __syscall_ret(__syscall(__VA_ARGS__))

#endif
