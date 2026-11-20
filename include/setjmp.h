#ifndef _SETJMP_H
#define _SETJMP_H

/*
 * RISC-V 64 non-local jump context.
 *
 * Layout (unsigned long slots):
 *   0-88   s0-s11        callee-saved integer registers
 *   96     ra
 *   104    sp
 *   112-200 fs0-fs11     callee-saved FP registers
 *   208    fcsr
 * The FP registers are saved because Lua (and any code the compiler may
 * generate) can hold live double values across setjmp in fs0-fs11.
 */

typedef struct {
	unsigned long __jb[27];
} jmp_buf[1];

int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif
