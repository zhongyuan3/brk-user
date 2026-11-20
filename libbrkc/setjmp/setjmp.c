/*
 * RISC-V 64 setjmp/longjmp.
 *
 * The naked attribute keeps the prologue empty, so the sp saved by setjmp
 * is exactly the caller's stack pointer and longjmp restores the caller
 * frame precisely.  Callee-saved FP registers and fcsr are part of the
 * context: Lua's error handling (LUAI_THROW) jumps across C frames that
 * may hold live doubles in fs0-fs11.
 */

#include <setjmp.h>

__attribute__((naked, returns_twice))
int setjmp(jmp_buf env)
{
	asm volatile(
		"	sd s0, 0(%0)\n"
		"	sd s1, 8(%0)\n"
		"	sd s2, 16(%0)\n"
		"	sd s3, 24(%0)\n"
		"	sd s4, 32(%0)\n"
		"	sd s5, 40(%0)\n"
		"	sd s6, 48(%0)\n"
		"	sd s7, 56(%0)\n"
		"	sd s8, 64(%0)\n"
		"	sd s9, 72(%0)\n"
		"	sd s10, 80(%0)\n"
		"	sd s11, 88(%0)\n"
		"	sd ra, 96(%0)\n"
		"	sd sp, 104(%0)\n"
		"	fsd fs0, 112(%0)\n"
		"	fsd fs1, 120(%0)\n"
		"	fsd fs2, 128(%0)\n"
		"	fsd fs3, 136(%0)\n"
		"	fsd fs4, 144(%0)\n"
		"	fsd fs5, 152(%0)\n"
		"	fsd fs6, 160(%0)\n"
		"	fsd fs7, 168(%0)\n"
		"	fsd fs8, 176(%0)\n"
		"	fsd fs9, 184(%0)\n"
		"	fsd fs10, 192(%0)\n"
		"	fsd fs11, 200(%0)\n"
		"	frcsr t0\n"
		"	sd t0, 208(%0)\n"
		"	li a0, 0\n"
		"	ret\n"
		:
		: "r"(env)
		: "memory");
	__builtin_unreachable();
}

__attribute__((naked, noreturn))
void longjmp(jmp_buf env, int val)
{
	asm volatile(
		"	ld s0, 0(%0)\n"
		"	ld s1, 8(%0)\n"
		"	ld s2, 16(%0)\n"
		"	ld s3, 24(%0)\n"
		"	ld s4, 32(%0)\n"
		"	ld s5, 40(%0)\n"
		"	ld s6, 48(%0)\n"
		"	ld s7, 56(%0)\n"
		"	ld s8, 64(%0)\n"
		"	ld s9, 72(%0)\n"
		"	ld s10, 80(%0)\n"
		"	ld s11, 88(%0)\n"
		"	ld ra, 96(%0)\n"
		"	ld sp, 104(%0)\n"
		"	ld t0, 208(%0)\n"
		"	fscsr t0\n"
		"	fld fs0, 112(%0)\n"
		"	fld fs1, 120(%0)\n"
		"	fld fs2, 128(%0)\n"
		"	fld fs3, 136(%0)\n"
		"	fld fs4, 144(%0)\n"
		"	fld fs5, 152(%0)\n"
		"	fld fs6, 160(%0)\n"
		"	fld fs7, 168(%0)\n"
		"	fld fs8, 176(%0)\n"
		"	fld fs9, 184(%0)\n"
		"	fld fs10, 192(%0)\n"
		"	fld fs11, 200(%0)\n"
		"	beqz %1, 1f\n"
		"	mv a0, %1\n"
		"	ret\n"
		"1:\n"
		"	li a0, 1\n"
		"	ret\n"
		:
		: "r"(env), "r"(val)
		: "memory");
	__builtin_unreachable();
}
