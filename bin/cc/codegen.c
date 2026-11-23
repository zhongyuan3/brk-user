#include "brkcc.h"
#include <uapi/syscall.h>

/* ------------------------------------------------------------------ */
/* code generation                                                     */
/* ------------------------------------------------------------------ */

static int cur_break = -1;
static int cur_cont = -1;
static struct obj *gen_fn;

struct clabel {
	char *name;
	int id;
	struct clabel *next;
};

static struct clabel *clabels;

static int clabel_get(const char *name)
{
	struct clabel *l;

	for (l = clabels; l; l = l->next) {
		if (!strcmp(l->name, name))
			return l->id;
	}
	l = xcalloc(1, sizeof(*l));
	l->name = xstrdup(name);
	l->id = new_label();
	l->next = clabels;
	clabels = l;
	return l->id;
}

static void check_clabels(void)
{
	struct clabel *l;

	for (l = clabels; l; l = l->next) {
		if (label_pos[l->id] < 0)
			fatal("undefined label '%s'", l->name);
	}
	clabels = NULL;
}

static void push_reg(int reg)
{
	i_addi(SP, SP, -16);
	i_sd(reg, SP, 0);
}

static void pop_reg(int reg)
{
	i_ld(reg, SP, 0);
	i_addi(SP, SP, 16);
}

/* ------------------------------------------------------------------ */
/* small instruction helpers                                           */
/* ------------------------------------------------------------------ */

static void i_slli(int rd, int rs, int sh)
{
	if (sh < 0 || sh > 63)
		fatal("internal error: bad shift amount");
	emit32(enc_i(0x13, rd, 1, rs, sh));
}

static void i_srli(int rd, int rs, int sh)
{
	if (sh < 0 || sh > 63)
		fatal("internal error: bad shift amount");
	emit32(enc_i(0x13, rd, 5, rs, sh));
}

static void i_srai(int rd, int rs, int sh)
{
	if (sh < 0 || sh > 63)
		fatal("internal error: bad shift amount");
	emit32(enc_i(0x13, rd, 5, rs, 0x400 | sh));
}

static void load_s0(int rd, int off)
{
	if (off >= -2048 && off <= 2047) {
		i_ld(rd, S0, off);
		return;
	}
	i_li(T6, off);
	i_r(0, 0, rd, S0, T6);
	i_ld(rd, rd, 0);
}

static void store_s1(int reg, int off)
{
	if (off >= -2048 && off <= 2047) {
		i_sd(reg, S1, off);
		return;
	}
	i_li(T6, off);
	i_r(0, 0, T6, S1, T6);
	i_sd(reg, T6, 0);
}

static void load_s1(int rd, int off)
{
	if (off >= -2048 && off <= 2047) {
		i_ld(rd, S1, off);
		return;
	}
	i_li(T6, off);
	i_r(0, 0, rd, S1, T6);
	i_ld(rd, rd, 0);
}

/* ------------------------------------------------------------------ */
/* floating point helpers                                              */
/* ------------------------------------------------------------------ */

static void fp_r(int f7, int rs2, int rs1, int f3, int rd)
{
	emit32(enc_r(f7, rs2, rs1, f3, rd, 0x53));
}

static void fp_from_int(int f, int r, bool d)
{
	emit32(enc_r(d ? 0x79 : 0x78, 0, r, 0, f, 0x53));
}

static void fp_to_int(int r, int f, bool d)
{
	emit32(enc_r(d ? 0x71 : 0x70, 0, f, 0, r, 0x53));
}

static void fp_const(int f, unsigned long bits, bool d)
{
	i_li(T0, (long)bits);
	fp_from_int(f, T0, d);
}

/* ------------------------------------------------------------------ */
/* types and conversions                                               */
/* ------------------------------------------------------------------ */

static void cast_to(struct type *ty)
{
	if (ty->kind == TY_VOID || ty->kind == TY_PTR || ty->kind == TY_ARRAY ||
	    ty->kind == TY_FUNC || is_float(ty))
		return;
	if (ty->size == 1) {
		if (ty->is_unsigned) {
			emit32(enc_i(0x13, A0, 7, A0, 0xff)); /* andi */
		} else {
			emit32(enc_i(0x13, A0, 1, A0, 56)); /* slli */
			emit32(enc_i(0x13, A0, 5, A0, 0x438)); /* srai */
		}
	} else if (ty->size == 2) {
		emit32(enc_i(0x13, A0, 1, A0, 48)); /* slli */
		if (ty->is_unsigned)
			emit32(enc_i(0x13, A0, 5, A0, 48)); /* srli */
		else
			emit32(enc_i(0x13, A0, 5, A0, 0x430)); /* srai */
	} else if (ty->size == 4) {
		if (ty->is_unsigned) {
			emit32(enc_i(0x13, A0, 1, A0, 32));
			emit32(enc_i(0x13, A0, 5, A0, 32));
		} else {
			emit32(enc_i(0x1b, A0, 0, A0, 0)); /* addiw a0,a0,0 */
		}
	}
}

static void int_to_fp(struct type *from, bool d)
{
	int rs2;

	if (from->kind == TY_PTR || from->size >= 8)
		rs2 = (from->is_unsigned || from->kind == TY_PTR) ? 3 : 2;
	else
		rs2 = from->is_unsigned ? 1 : 0;
	fp_r(d ? 0x69 : 0x68, rs2, A0, 0, F0);
	fp_to_int(A0, F0, d);
}

static void fp_to_int_value(struct type *to, bool d)
{
	int rs2;

	if (to->kind == TY_PTR || to->size >= 8)
		rs2 = (to->is_unsigned || to->kind == TY_PTR) ? 3 : 2;
	else
		rs2 = to->is_unsigned ? 1 : 0;
	fp_r(d ? 0x61 : 0x60, rs2, F0, 1, A0); /* fcvt.*.[sd], RTZ */
	cast_to(to);
}

static void cast_convert(struct type *from, struct type *to)
{
	from = decay(from);
	to = decay(to);
	if (to->kind == TY_VOID)
		return;
	if (is_aggregate(from) || is_aggregate(to))
		fatal("cannot cast a struct/union value");

	if (is_float(from)) {
		bool d = from->kind == TY_DOUBLE;

		if (is_float(to)) {
			if (to->kind == from->kind)
				return;
			fp_from_int(F0, A0, d);
			if (to->kind == TY_DOUBLE) {
				fp_r(0x21, 0, F0, 0, F0); /* fcvt.d.s */
				fp_to_int(A0, F0, true);
			} else {
				fp_r(0x20, 1, F0, 0, F0); /* fcvt.s.d */
				fp_to_int(A0, F0, false);
			}
			return;
		}
		if (is_integer(to) || to->kind == TY_PTR) {
			fp_from_int(F0, A0, d);
			fp_to_int_value(to, d);
			return;
		}
		fatal("invalid floating-point cast");
	}

	if (is_float(to)) {
		if (!is_integer(from) && from->kind != TY_PTR)
			fatal("invalid cast to a floating-point type");
		cast_to(from);
		int_to_fp(from, to->kind == TY_DOUBLE);
		return;
	}

	if (to->kind == TY_PTR || to->kind == TY_ARRAY ||
	    to->kind == TY_FUNC)
		return;
	if (is_integer(to))
		cast_to(to);
}

static void load_value(struct type *ty)
{
	if (ty->kind == TY_ARRAY || ty->kind == TY_FUNC ||
	    ty->kind == TY_VOID || is_aggregate(ty))
		return;
	if (ty->kind == TY_FLOAT) {
		i_lw(A0, A0, 0);
		return;
	}
	if (ty->kind == TY_DOUBLE) {
		i_ld(A0, A0, 0);
		return;
	}
	switch (ty->size) {
	case 1:
		if (ty->is_unsigned)
			i_lbu(A0, A0, 0);
		else
			i_lb(A0, A0, 0);
		break;
	case 2:
		if (ty->is_unsigned)
			i_lhu(A0, A0, 0);
		else
			i_lh(A0, A0, 0);
		break;
	case 4:
		if (ty->is_unsigned)
			i_lwu(A0, A0, 0);
		else
			i_lw(A0, A0, 0);
		break;
	case 8:
		i_ld(A0, A0, 0);
		break;
	default:
		fatal("internal error: bad load size");
	}
}

static void store_value(struct type *ty)
{
	/* address in a0, value in a1 */
	if (is_aggregate(ty))
		fatal("internal error: aggregate store");
	switch (ty->size) {
	case 1:
		i_sb(A1, A0, 0);
		break;
	case 2:
		i_sh(A1, A0, 0);
		break;
	case 4:
		i_sw(A1, A0, 0);
		break;
	case 8:
		i_sd(A1, A0, 0);
		break;
	default:
		fatal("internal error: bad store size");
	}
}

static void load_unit_to(int rd, int rs, int size)
{
	switch (size) {
	case 1:
		i_lbu(rd, rs, 0);
		break;
	case 2:
		i_lhu(rd, rs, 0);
		break;
	case 4:
		i_lwu(rd, rs, 0);
		break;
	case 8:
		i_ld(rd, rs, 0);
		break;
	default:
		fatal("internal error: bad bit-field storage size");
	}
}

static void store_unit_from(int rs2, int rs1, int size)
{
	switch (size) {
	case 1:
		i_sb(rs2, rs1, 0);
		break;
	case 2:
		i_sh(rs2, rs1, 0);
		break;
	case 4:
		i_sw(rs2, rs1, 0);
		break;
	case 8:
		i_sd(rs2, rs1, 0);
		break;
	default:
		fatal("internal error: bad bit-field storage size");
	}
}

static int type_step(struct type *ty)
{
	struct type *t = decay(ty);
	int step;

	if (t->kind != TY_PTR)
		return 1;
	step = t->base->size;
	if (step <= 0)
		fatal("pointer arithmetic on an incomplete type");
	return step;
}

static bool returns_aggregate(struct obj *fn)
{
	return fn->ty && fn->ty->base && is_aggregate(fn->ty->base);
}

/* ------------------------------------------------------------------ */
/* bit-fields                                                          */
/* ------------------------------------------------------------------ */

static unsigned long bf_mask(int width)
{
	if (width <= 0 || width > 64)
		fatal("internal error: bad bit-field width");
	if (width == 64)
		return ~0UL;
	return (1UL << width) - 1;
}

/* Address in a0; leave the field value in a0. */
static void load_bitfield(struct node *n)
{
	int width = n->bit_width;
	unsigned long mask = bf_mask(width);

	load_unit_to(A0, A0, n->ty->size);
	if (n->bit_offset)
		i_srli(A0, A0, n->bit_offset);
	if (mask != ~0UL) {
		i_li(T0, (long)mask);
		i_r(0, 7, A0, A0, T0); /* and */
	}
	if (!n->ty->is_unsigned && width < 64) {
		int sh = 64 - width;

		i_slli(A0, A0, sh);
		i_srai(A0, A0, sh);
	} else {
		cast_to(n->ty);
	}
}

/* Value in a0, storage-unit address in t1; leave the field value in a0. */
static void store_bitfield(struct node *n)
{
	int width = n->bit_width;
	int off = n->bit_offset;
	unsigned long mask = bf_mask(width);
	unsigned long shifted = mask << off;

	i_mv(T2, A0);
	load_unit_to(T0, T1, n->ty->size);
	if (shifted != ~0UL) {
		i_li(A0, (long)~shifted);
		i_r(0, 7, T0, T0, A0); /* and */
	}
	if (off)
		i_slli(T2, T2, off);
	if (shifted != ~0UL) {
		i_li(A0, (long)shifted);
		i_r(0, 7, T2, T2, A0); /* and */
	}
	i_r(0, 6, T0, T0, T2); /* or */
	store_unit_from(T0, T1, n->ty->size);

	if (off)
		i_srli(T2, T2, off);
	if (!n->ty->is_unsigned && width < 64) {
		int sh = 64 - width;

		i_slli(T2, T2, sh);
		i_srai(T2, T2, sh);
	}
	i_mv(A0, T2);
	cast_to(n->ty);
}

/* ------------------------------------------------------------------ */
/* expression generation                                               */
/* ------------------------------------------------------------------ */

static void gen_expr(struct node *n);
static void gen_stmt(struct node *n);

static void gen_addr(struct node *n)
{
	if (n->kind == ND_VAR) {
		if (n->var->is_local) {
			i_addi(A0, S0, -n->var->offset);
		} else {
			i_la(A0, n->var->sym, 0);
		}
		return;
	}
	if (n->kind == ND_DEREF) {
		gen_expr(n->lhs);
		return;
	}
	if (n->kind == ND_MEMBER) {
		gen_addr(n->lhs);
		if (n->member_offset)
			i_addi(A0, A0, n->member_offset);
		return;
	}
	/*
	 * Aggregate-valued rvalues also denote a storage location: a call
	 * returns the address of its hidden result temporary, an aggregate
	 * assignment returns its destination, and conditional/comma
	 * expressions return one of their branch addresses.
	 */
	if (n->kind == ND_CALL || n->kind == ND_ASSIGN || n->kind == ND_COND ||
	    n->kind == ND_COMMA) {
		gen_expr(n);
		return;
	}
	if (n->kind == ND_STR) {
		gen_expr(n);
		return;
	}
	fatal("not an lvalue");
}

static void gen_cond_expr(struct node *n)
{
	struct type *t;

	gen_expr(n);
	t = n->ty ? decay(n->ty) : NULL;
	if (t && is_float(t)) {
		bool d = t->kind == TY_DOUBLE;

		fp_from_int(F0, A0, d);
		i_li(T0, 0);
		fp_from_int(F1, T0, d);
		fp_r(d ? 0x51 : 0x50, F1, F0, 2, A0); /* feq */
		emit32(enc_i(0x13, A0, 4, A0, 1)); /* xori a0,a0,1 */
	}
}

static struct sym *puts_sym;
static struct sym *memcpy_sym;
static struct sym *memset_sym;

static int builtin_syscall(const char *name)
{
	if (!strcmp(name, "write"))
		return SYS_WRITE;
	if (!strcmp(name, "read"))
		return SYS_READ;
	if (!strcmp(name, "close"))
		return SYS_CLOSE;
	if (!strcmp(name, "open"))
		return SYS_OPEN;
	return -1;
}

static void gen_call(struct node *n)
{
	const char *name = n->var ? n->var->name : NULL;
	bool direct = n->var != NULL;
	bool builtin_ok = direct && !n->var->defined;
	struct type *rt = n->ty;
	bool hidden;
	bool indirect = n->callee != NULL;
	int nslots;
	int total;
	int sysno = -1;
	int i;

	if (direct && !strcmp(name, "__builtin_va_start")) {
		int off;

		if (!gen_fn || !gen_fn->va_area)
			fatal("va_start used outside a variadic function");
		off = -(gen_fn->va_area->offset) +
		      (gen_fn->ty->nparams +
		       (returns_aggregate(gen_fn) ? 1 : 0)) *
			      8;
		i_addi(A0, S0, off);
		return;
	}

	if (builtin_ok && !strcmp(name, "putchar")) {
		if (n->nargs != 1)
			fatal("putchar expects one argument");
		gen_expr(n->args[0]);
		i_addi(SP, SP, -16);
		i_sb(A0, SP, 0);
		i_li(A0, 1);
		i_mv(A1, SP);
		i_li(A2, 1);
		i_li(17, SYS_WRITE); /* a7 = SYS_WRITE */
		i_ecall();
		i_addi(SP, SP, 16);
		return;
	}
	if (builtin_ok && !strcmp(name, "puts")) {
		if (n->nargs != 1)
			fatal("puts expects one argument");
		if (!puts_sym)
			fatal("internal error: missing puts runtime reference");
		gen_expr(n->args[0]);
		i_call_sym(puts_sym);
		return;
	}
	if (builtin_ok && !strcmp(name, "exit")) {
		if (n->nargs > 0)
			gen_expr(n->args[0]);
		else
			i_li(A0, 0);
		i_li(17, SYS_EXIT); /* a7 = SYS_EXIT */
		i_ecall();
		emit32(enc_j(0x6f, X0, 0));
		return;
	}
	if (builtin_ok)
		sysno = builtin_syscall(name);

	if (!rt) {
		if (n->var && n->var->ty && n->var->ty->kind == TY_FUNC)
			rt = n->var->ty->base;
		else
			rt = ty_int;
	}
	hidden = is_aggregate(rt);
	if (hidden && !n->ret_tmp)
		fatal("internal error: missing aggregate-return temporary");

	nslots = n->nargs + (hidden ? 1 : 0);
	total = align_to((nslots + (indirect ? 1 : 0)) * 8, 16);
	push_reg(S1);
	i_addi(SP, SP, -total);
	i_mv(S1, SP);

	if (hidden) {
		i_addi(A0, S0, -n->ret_tmp->offset);
		i_sd(A0, S1, 0);
	}
	if (indirect) {
		gen_expr(n->callee);
		i_mv(S1, SP);
		store_s1(A0, nslots * 8);
	}
	for (i = n->nargs - 1; i >= 0; i--) {
		struct node *arg = n->args[i];
		int slot = i + (hidden ? 1 : 0);

		if (is_aggregate(arg->ty)) {
			struct obj *tmp = n->arg_temps ? n->arg_temps[i] : NULL;

			if (!tmp)
				fatal("internal error: missing aggregate argument copy");
			if (!memcpy_sym)
				fatal("internal error: missing memcpy runtime reference");
			gen_expr(arg);
			i_mv(A1, A0);
			i_addi(A0, S0, -tmp->offset);
			i_li(A2, arg->ty->size);
			i_call_sym(memcpy_sym);
			i_mv(S1, SP);
			store_s1(A0, slot * 8);
		} else {
			gen_expr(arg);
			i_mv(S1, SP);
			store_s1(A0, slot * 8);
		}
	}

	i_mv(S1, SP);
	for (i = 0; i < 8; i++)
		load_s1(A0 + i, i * 8);
	if (indirect)
		load_s1(T2, nslots * 8);
	if (nslots > 8)
		i_addi(SP, S1, 64);
	else
		i_mv(SP, S1);

	if (sysno == SYS_OPEN) {
		/*
		 * The kernel entry named SYS_OPEN is openat(dirfd, path, flags,
		 * mode).  The C builtin is open(path, flags[, mode]), so shift
		 * the arguments up and pass AT_FDCWD in a0.  A two-argument
		 * open() implies mode 0, matching the common non-O_CREAT use.
		 */
		if (n->nargs == 2) {
			i_li(A0 + 3, 0);
			i_mv(A2, A1);
			i_mv(A1, A0);
			i_li(A0, -100); /* AT_FDCWD */
		} else if (n->nargs == 3) {
			i_mv(A0 + 3, A2);
			i_mv(A2, A1);
			i_mv(A1, A0);
			i_li(A0, -100); /* AT_FDCWD */
		} else {
			fatal("open expects two or three arguments");
		}
	}

	if (sysno >= 0) {
		i_li(17, sysno);
		i_ecall();
	} else if (direct) {
		if (!n->var->sym)
			fatal("internal error: call to a function without a symbol");
		i_call_sym(n->var->sym);
	} else {
		emit32(enc_i(0x67, RA, 0, T2, 0)); /* jalr ra,0(t2) */
	}
	i_addi(SP, S1, total);
	pop_reg(S1);

	if (hidden) {
		i_addi(A0, S0, -n->ret_tmp->offset);
		return;
	}
	cast_to(rt);
}

static void gen_fp_bin(struct node *n, struct type *lt, struct type *rt)
{
	bool d = lt->kind == TY_DOUBLE || rt->kind == TY_DOUBLE;
	struct type *target = d ? ty_double : ty_float;
	int eqf7 = d ? 0x51 : 0x50;

	switch (n->kind) {
	case ND_ADD:
	case ND_SUB:
	case ND_MUL:
	case ND_DIV:
	case ND_EQ:
	case ND_NE:
	case ND_LT:
	case ND_LE:
	case ND_GT:
	case ND_GE:
		break;
	default:
		fatal("invalid floating-point operator");
	}

	gen_expr(n->lhs);
	cast_convert(lt, target);
	push_reg(A0);
	gen_expr(n->rhs);
	cast_convert(rt, target);
	i_mv(A1, A0);
	pop_reg(A0);

	fp_from_int(F0, A0, d);
	fp_from_int(F1, A1, d);

	switch (n->kind) {
	case ND_ADD:
		fp_r(d ? 0x01 : 0x00, F1, F0, 0, F0);
		fp_to_int(A0, F0, d);
		return;
	case ND_SUB:
		fp_r(d ? 0x05 : 0x04, F1, F0, 0, F0);
		fp_to_int(A0, F0, d);
		return;
	case ND_MUL:
		fp_r(d ? 0x09 : 0x08, F1, F0, 0, F0);
		fp_to_int(A0, F0, d);
		return;
	case ND_DIV:
		fp_r(d ? 0x0d : 0x0c, F1, F0, 0, F0);
		fp_to_int(A0, F0, d);
		return;
	case ND_EQ:
		fp_r(eqf7, F1, F0, 2, A0); /* feq */
		return;
	case ND_NE:
		fp_r(eqf7, F1, F0, 2, A0); /* feq */
		emit32(enc_i(0x13, A0, 4, A0, 1)); /* xori */
		return;
	case ND_LT:
		fp_r(eqf7, F1, F0, 1, A0); /* flt */
		return;
	case ND_LE:
		fp_r(eqf7, F1, F0, 0, A0); /* fle */
		return;
	case ND_GT:
		fp_r(eqf7, F0, F1, 1, A0); /* flt f1,f0 */
		return;
	default: /* ND_GE */
		fp_r(eqf7, F0, F1, 0, A0); /* fle f1,f0 */
		return;
	}
}

static void gen_int_binop(int kind, struct type *lty)
{
	switch (kind) {
	case ND_ADD:
		if (is_ptr_like(lty)) {
			int step = type_step(lty);

			if (step != 1) {
				i_li(T0, step);
				i_r(1, 0, A1, A1, T0); /* mul */
			}
		}
		i_r(0, 0, A0, A0, A1);
		break;
	case ND_SUB:
		if (is_ptr_like(lty)) {
			int step = type_step(lty);

			if (step != 1) {
				i_li(T0, step);
				i_r(1, 0, A1, A1, T0);
			}
		}
		i_r(0x20, 0, A0, A0, A1);
		break;
	case ND_MUL:
		i_r(1, 0, A0, A0, A1);
		break;
	case ND_DIV:
		i_r(1, lty->is_unsigned ? 5 : 4, A0, A0, A1);
		break;
	case ND_MOD:
		i_r(1, lty->is_unsigned ? 7 : 6, A0, A0, A1);
		break;
	case ND_BITAND:
		i_r(0, 7, A0, A0, A1);
		break;
	case ND_BITOR:
		i_r(0, 6, A0, A0, A1);
		break;
	case ND_BITXOR:
		i_r(0, 4, A0, A0, A1);
		break;
	case ND_SHL:
		i_r(0, 1, A0, A0, A1);
		break;
	case ND_SHR:
		i_r(lty->is_unsigned ? 0 : 0x20, 5, A0, A0, A1);
		break;
	default:
		fatal("internal error: bad binary operator");
	}
}

static void gen_bin(struct node *n)
{
	struct type *lt = decay(n->lhs->ty);
	struct type *rt = decay(n->rhs->ty);
	int unsigned_cmp;

	if (is_float(lt) || is_float(rt)) {
		gen_fp_bin(n, lt, rt);
		return;
	}

	unsigned_cmp = lt->is_unsigned || rt->is_unsigned ||
		       lt->kind == TY_PTR || rt->kind == TY_PTR;
	gen_expr(n->lhs);
	push_reg(A0);
	gen_expr(n->rhs);
	i_mv(A1, A0);
	pop_reg(A0);

	switch (n->kind) {
	case ND_ADD:
		if (is_ptr_like(lt) && is_integer(rt)) {
			int step = type_step(lt);

			if (step != 1) {
				i_li(T0, step);
				i_r(1, 0, A1, A1, T0); /* mul a1,a1,t0 */
			}
			i_r(0, 0, A0, A0, A1);
		} else if (is_ptr_like(rt) && is_integer(lt)) {
			int step = type_step(rt);

			if (step != 1) {
				i_li(T0, step);
				i_r(1, 0, A0, A0, T0);
			}
			i_r(0, 0, A0, A1, A0); /* add a0,a1,a0 */
		} else {
			i_r(0, 0, A0, A0, A1);
		}
		break;
	case ND_SUB:
		if (is_ptr_like(lt) && is_ptr_like(rt)) {
			int step = type_step(lt);

			i_r(0x20, 0, A0, A0, A1); /* sub */
			if (step != 1) {
				i_li(A1, step);
				i_r(1, 4, A0, A0, A1); /* div */
			}
		} else if (is_ptr_like(lt) && is_integer(rt)) {
			int step = type_step(lt);

			if (step != 1) {
				i_li(T0, step);
				i_r(1, 0, A1, A1, T0);
			}
			i_r(0x20, 0, A0, A0, A1);
		} else {
			i_r(0x20, 0, A0, A0, A1);
		}
		break;
	case ND_MUL:
		i_r(1, 0, A0, A0, A1);
		break;
	case ND_DIV:
		i_r(1, unsigned_cmp ? 5 : 4, A0, A0, A1);
		break;
	case ND_MOD:
		i_r(1, unsigned_cmp ? 7 : 6, A0, A0, A1);
		break;
	case ND_BITAND:
		i_r(0, 7, A0, A0, A1);
		break;
	case ND_BITOR:
		i_r(0, 6, A0, A0, A1);
		break;
	case ND_BITXOR:
		i_r(0, 4, A0, A0, A1);
		break;
	case ND_SHL:
		i_r(0, 1, A0, A0, A1);
		break;
	case ND_SHR:
		i_r(lt->is_unsigned ? 0 : 0x20, 5, A0, A0, A1);
		break;
	case ND_EQ:
		i_r(0, 4, A0, A0, A1); /* xor */
		i_seqz(A0, A0);
		return;
	case ND_NE:
		i_r(0, 4, A0, A0, A1);
		i_snez(A0, A0);
		return;
	case ND_LT:
		emit32(enc_r(0, A1, A0, unsigned_cmp ? 3 : 2, A0, 0x33));
		return;
	case ND_GT:
		emit32(enc_r(0, A0, A1, unsigned_cmp ? 3 : 2, A0, 0x33));
		return;
	case ND_LE:
		emit32(enc_r(0, A0, A1, unsigned_cmp ? 3 : 2, A0, 0x33));
		emit32(enc_i(0x13, A0, 4, A0, 1)); /* xori a0,a0,1 */
		return;
	case ND_GE:
		emit32(enc_r(0, A1, A0, unsigned_cmp ? 3 : 2, A0, 0x33));
		emit32(enc_i(0x13, A0, 4, A0, 1));
		return;
	default:
		fatal("internal error: bad binary node");
	}
	cast_to(n->ty);
}

static void gen_opassign(struct node *n)
{
	struct type *lty = decay(n->lhs->ty);
	struct type *rty = decay(n->rhs->ty);

	if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
		gen_addr(n->lhs);
		push_reg(A0);
		load_bitfield(n->lhs);
		push_reg(A0);
		gen_expr(n->rhs);
		i_mv(A1, A0);
		pop_reg(A0);
		gen_int_binop(n->val, lty);
		pop_reg(T1);
		store_bitfield(n->lhs);
		return;
	}

	gen_addr(n->lhs);
	push_reg(A0);
	load_value(n->lhs->ty);
	push_reg(A0);
	gen_expr(n->rhs);
	if (is_float(lty) || is_float(rty)) {
		bool d = lty->kind == TY_DOUBLE || rty->kind == TY_DOUBLE;
		struct type *target = d ? ty_double : ty_float;

		cast_convert(rty, target);
		i_mv(A1, A0);
		pop_reg(A0);
		cast_convert(lty, target);
		fp_from_int(F0, A0, d);
		fp_from_int(F1, A1, d);
		switch (n->val) {
		case ND_ADD:
			fp_r(d ? 0x01 : 0x00, F1, F0, 0, F0);
			break;
		case ND_SUB:
			fp_r(d ? 0x05 : 0x04, F1, F0, 0, F0);
			break;
		case ND_MUL:
			fp_r(d ? 0x09 : 0x08, F1, F0, 0, F0);
			break;
		case ND_DIV:
			fp_r(d ? 0x0d : 0x0c, F1, F0, 0, F0);
			break;
		default:
			fatal("invalid floating-point compound assignment");
		}
		fp_to_int(A0, F0, d);
		if (!is_float(lty))
			cast_convert(target, lty);
	} else {
		i_mv(A1, A0);
		pop_reg(A0);
		gen_int_binop(n->val, lty);
	}
	push_reg(A0);
	pop_reg(A1);
	pop_reg(A0);
	store_value(n->lhs->ty);
	i_mv(A0, A1);
	cast_to(n->lhs->ty);
}

static void gen_incdec(struct node *n)
{
	struct type *lty = decay(n->lhs->ty);
	int delta = (int)n->val;
	int prefix = n->kind == ND_PREINC;

	if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
		gen_addr(n->lhs);
		push_reg(A0);
		load_bitfield(n->lhs);
		i_mv(T6, A0);
		i_addi(A0, A0, delta);
		pop_reg(T1);
		store_bitfield(n->lhs);
		if (!prefix)
			i_mv(A0, T6);
		return;
	}

	if (is_float(lty)) {
		bool d = lty->kind == TY_DOUBLE;

		gen_addr(n->lhs);
		push_reg(A0);
		load_value(n->lhs->ty);
		push_reg(A0);
		fp_from_int(F0, A0, d);
		if (d)
			fp_const(F1,
				 delta > 0 ? 0x3ff0000000000000UL :
					     0xbff0000000000000UL,
				 true);
		else
			fp_const(F1, delta > 0 ? 0x3f800000UL : 0xbf800000UL,
				 false);
		fp_r(d ? 0x01 : 0x00, F1, F0, 0, F0);
		fp_to_int(A0, F0, d);
		push_reg(A0);
		pop_reg(A1); /* new value */
		pop_reg(T0); /* old value */
		pop_reg(T1); /* address */
		i_mv(A0, T1);
		store_value(n->lhs->ty);
		i_mv(A0, prefix ? A1 : T0);
		cast_to(n->lhs->ty);
		return;
	}

	{
		int step = type_step(n->lhs->ty);

		gen_addr(n->lhs);
		push_reg(A0);
		load_value(n->lhs->ty);
		push_reg(A0);
		if (step != 1) {
			i_li(T0, step * delta);
			i_r(0, 0, A0, A0, T0);
		} else {
			i_addi(A0, A0, delta);
		}
		push_reg(A0);
		pop_reg(A1); /* new value */
		pop_reg(T0); /* old value */
		pop_reg(T1); /* address */
		i_mv(A0, T1);
		store_value(n->lhs->ty);
		i_mv(A0, prefix ? A1 : T0);
		cast_to(n->lhs->ty);
	}
}

static void gen_float_const(struct node *n)
{
	if (n->ty->kind == TY_FLOAT) {
		float f = (float)n->fval;
		unsigned int u = 0;

		memcpy(&u, &f, sizeof(u));
		i_li(A0, (long)(unsigned long)u);
	} else {
		double d = n->fval;
		unsigned long u = 0;

		memcpy(&u, &d, sizeof(u));
		i_li(A0, (long)u);
	}
}

static void gen_expr(struct node *n)
{
	switch (n->kind) {
	case ND_NULL:
		return;
	case ND_NUM:
		i_li(A0, n->val);
		return;
	case ND_FLOAT:
		gen_float_const(n);
		return;
	case ND_STR:
		i_la(A0, n->sym, 0);
		return;
	case ND_VAR:
		if (n->var->is_function) {
			i_la(A0, n->var->sym, 0);
			return;
		}
		gen_addr(n);
		load_value(n->ty);
		return;
	case ND_DEREF:
		gen_expr(n->lhs);
		load_value(n->ty);
		return;
	case ND_MEMBER:
		gen_addr(n);
		if (n->bit_width > 0)
			load_bitfield(n);
		else
			load_value(n->ty);
		return;
	case ND_ADDR:
		gen_addr(n->lhs);
		return;
	case ND_ASSIGN:
		if (is_aggregate(n->lhs->ty)) {
			gen_addr(n->lhs);
			push_reg(A0);
			gen_expr(n->rhs);
			i_mv(A1, A0);
			pop_reg(A0);
			i_li(A2, n->lhs->ty->size);
			if (!memcpy_sym)
				fatal("internal error: missing memcpy runtime reference");
			i_call_sym(memcpy_sym);
			return;
		}
		if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
			gen_addr(n->lhs);
			push_reg(A0);
			gen_expr(n->rhs);
			cast_convert(n->rhs->ty, n->lhs->ty);
			pop_reg(T1);
			store_bitfield(n->lhs);
			return;
		}
		gen_addr(n->lhs);
		push_reg(A0);
		gen_expr(n->rhs);
		cast_convert(n->rhs->ty, n->lhs->ty);
		i_mv(A1, A0);
		pop_reg(A0);
		store_value(n->lhs->ty);
		i_mv(A0, A1);
		cast_to(n->lhs->ty);
		return;
	case ND_OPASSIGN:
		gen_opassign(n);
		return;
	case ND_PREINC:
	case ND_POSTINC:
		gen_incdec(n);
		return;
	case ND_CAST:
		gen_expr(n->lhs);
		cast_convert(n->lhs->ty, n->ty);
		return;
	case ND_COMMA:
		gen_expr(n->lhs);
		gen_expr(n->rhs);
		return;
	case ND_COND: {
		int lelse = new_label();
		int lend = new_label();

		gen_cond_expr(n->cond);
		jump_if_zero(A0, lelse);
		gen_expr(n->then);
		jump_to(lend);
		place_label(lelse);
		gen_expr(n->els);
		place_label(lend);
		return;
	}
	case ND_LOGAND: {
		int lfalse = new_label();
		int lend = new_label();

		gen_cond_expr(n->lhs);
		jump_if_zero(A0, lfalse);
		gen_cond_expr(n->rhs);
		jump_if_zero(A0, lfalse);
		i_li(A0, 1);
		jump_to(lend);
		place_label(lfalse);
		i_li(A0, 0);
		place_label(lend);
		return;
	}
	case ND_LOGOR: {
		int ltrue = new_label();
		int lend = new_label();

		gen_cond_expr(n->lhs);
		jump_if_nonzero(A0, ltrue);
		gen_cond_expr(n->rhs);
		jump_if_nonzero(A0, ltrue);
		i_li(A0, 0);
		jump_to(lend);
		place_label(ltrue);
		i_li(A0, 1);
		place_label(lend);
		return;
	}
	case ND_NEG:
		gen_expr(n->lhs);
		if (n->ty && is_float(decay(n->ty))) {
			bool d = decay(n->ty)->kind == TY_DOUBLE;

			fp_from_int(F0, A0, d);
			fp_r(d ? 0x11 : 0x10, F0, F0, 1, F0); /* fsgnjn */
			fp_to_int(A0, F0, d);
			return;
		}
		i_r(0x20, 0, A0, X0, A0);
		cast_to(n->ty);
		return;
	case ND_NOT:
		gen_cond_expr(n->lhs);
		emit32(enc_i(0x13, A0, 4, A0, 1)); /* xori */
		return;
	case ND_BITNOT:
		if (n->ty && is_float(decay(n->ty)))
			fatal("invalid bitwise negation of a floating-point value");
		gen_expr(n->lhs);
		emit32(enc_i(0x13, A0, 4, A0, -1)); /* xori */
		return;
	case ND_ADD:
	case ND_SUB:
	case ND_MUL:
	case ND_DIV:
	case ND_MOD:
	case ND_BITAND:
	case ND_BITOR:
	case ND_BITXOR:
	case ND_SHL:
	case ND_SHR:
	case ND_EQ:
	case ND_NE:
	case ND_LT:
	case ND_LE:
	case ND_GT:
	case ND_GE:
		gen_bin(n);
		return;
	case ND_CALL:
		gen_call(n);
		return;
	default:
		fatal("internal error: bad expression node");
	}
}

static void gen_epilogue(void)
{
	i_mv(SP, S0);
	i_ld(RA, SP, -8);
	i_ld(S1, SP, -24);
	i_ld(S0, SP, -16);
	i_ret();
}

static void gen_default_return(struct obj *fn)
{
	if (returns_aggregate(fn)) {
		if (!fn->ret_ptr)
			fatal("internal error: missing aggregate-return pointer");
		i_addi(T0, S0, -fn->ret_ptr->offset);
		i_ld(A0, T0, 0);
	} else {
		i_li(A0, 0);
	}
	gen_epilogue();
}

static void gen_return(struct node *n)
{
	struct type *rt = gen_fn->ty->base;

	if (n->lhs && is_aggregate(rt)) {
		if (!gen_fn->ret_ptr)
			fatal("internal error: missing aggregate-return pointer");
		if (!memcpy_sym)
			fatal("internal error: missing memcpy runtime reference");
		gen_expr(n->lhs);
		i_mv(A1, A0);
		i_addi(T0, S0, -gen_fn->ret_ptr->offset);
		i_ld(A0, T0, 0); /* hidden result pointer */
		i_li(A2, rt->size);
		i_call_sym(memcpy_sym);
		i_addi(T0, S0, -gen_fn->ret_ptr->offset);
		i_ld(A0, T0, 0);
		gen_epilogue();
		return;
	}
	if (n->lhs) {
		gen_expr(n->lhs);
		if (rt->kind != TY_VOID && !is_aggregate(rt))
			cast_convert(n->lhs->ty, rt);
		else if (rt->kind == TY_VOID)
			i_li(A0, 0);
	} else {
		gen_default_return(gen_fn);
		return;
	}
	gen_epilogue();
}

static void gen_stmt(struct node *n)
{
	if (!n)
		return;
	switch (n->kind) {
	case ND_NULL:
		return;
	case ND_MEMZERO:
		gen_addr(n->lhs);
		i_li(A1, 0);
		i_li(A2, n->val);
		i_call_sym(memset_sym);
		return;
	case ND_BLOCK:
		for (struct node *s = n->body; s; s = s->next)
			gen_stmt(s);
		return;
	case ND_EXPR_STMT:
		gen_expr(n->lhs);
		return;
	case ND_RETURN:
		gen_return(n);
		return;
	case ND_IF: {
		int lelse = new_label();
		int lend = new_label();

		gen_cond_expr(n->cond);
		jump_if_zero(A0, lelse);
		gen_stmt(n->then);
		if (n->els) {
			jump_to(lend);
			place_label(lelse);
			gen_stmt(n->els);
			place_label(lend);
		} else {
			place_label(lelse);
		}
		return;
	}
	case ND_WHILE: {
		int lbegin = new_label();
		int lend = new_label();
		int old_break = cur_break;
		int old_cont = cur_cont;

		cur_break = lend;
		cur_cont = lbegin;
		place_label(lbegin);
		gen_cond_expr(n->cond);
		jump_if_zero(A0, lend);
		gen_stmt(n->body);
		jump_to(lbegin);
		place_label(lend);
		cur_break = old_break;
		cur_cont = old_cont;
		return;
	}
	case ND_FOR: {
		int lbegin = new_label();
		int lcont = new_label();
		int lend = new_label();
		int old_break = cur_break;
		int old_cont = cur_cont;

		cur_break = lend;
		cur_cont = lcont;
		gen_stmt(n->init);
		place_label(lbegin);
		if (n->cond) {
			gen_cond_expr(n->cond);
			jump_if_zero(A0, lend);
		}
		gen_stmt(n->body);
		place_label(lcont);
		if (n->inc)
			gen_expr(n->inc);
		jump_to(lbegin);
		place_label(lend);
		cur_break = old_break;
		cur_cont = old_cont;
		return;
	}
	case ND_DO: {
		int lbegin = new_label();
		int lcont = new_label();
		int lend = new_label();
		int old_break = cur_break;
		int old_cont = cur_cont;

		cur_break = lend;
		cur_cont = lcont;
		place_label(lbegin);
		gen_stmt(n->body);
		place_label(lcont);
		gen_cond_expr(n->cond);
		jump_if_nonzero(A0, lbegin);
		place_label(lend);
		cur_break = old_break;
		cur_cont = old_cont;
		return;
	}
	case ND_BREAK:
		if (cur_break < 0)
			fatal("break outside loop");
		jump_to(cur_break);
		return;
	case ND_CONTINUE:
		if (cur_cont < 0)
			fatal("continue outside loop");
		jump_to(cur_cont);
		return;
	case ND_SWITCH: {
		int lend = new_label();
		int old_break = cur_break;
		struct case_entry *e;
		struct case_entry *def = NULL;

		gen_expr(n->cond);
		i_mv(A1, A0);
		for (e = n->cases; e; e = e->next) {
			if (e->is_default) {
				def = e;
				continue;
			}
			i_li(T0, e->val);
			i_bf(1, A1, T0, 8); /* bne a1, t0, +8 */
			add_fixup(e->node->case_label);
		}
		if (def)
			add_fixup(def->node->case_label);
		else
			jump_to(lend);
		cur_break = lend;
		gen_stmt(n->body);
		place_label(lend);
		cur_break = old_break;
		return;
	}
	case ND_CASE:
		place_label(n->case_label);
		gen_stmt(n->body);
		return;
	case ND_DEFAULT:
		place_label(n->case_label);
		gen_stmt(n->body);
		return;
	case ND_GOTO:
		jump_to(clabel_get(n->label_name));
		return;
	case ND_LABEL: {
		int id = clabel_get(n->label_name);

		if (label_pos[id] >= 0)
			fatal("duplicate label '%s'", n->label_name);
		place_label(id);
		gen_stmt(n->body);
		return;
	}
	default:
		fatal("internal error: bad statement node %d", n->kind);
	}
}

/* ------------------------------------------------------------------ */
/* function and runtime emission                                       */
/* ------------------------------------------------------------------ */

static void emit_prologue(struct obj *fn)
{
	int f = fn->frame_size;
	bool hidden = returns_aggregate(fn);
	int i;

	if (f < 32)
		f = 32;
	f = align_to(f, 16);

	/* Preserve the caller's s0 before s0 becomes the new frame pointer. */
	i_mv(T0, S0);
	i_addi(SP, SP, -f);
	i_addi(S0, SP, f);
	i_sd(RA, S0, -8);
	i_sd(T0, S0, -16);
	i_sd(S1, S0, -24);

	if (hidden) {
		if (!fn->ret_ptr)
			fatal("internal error: missing aggregate-return pointer");
		i_addi(T0, S0, -fn->ret_ptr->offset);
		i_sd(A0, T0, 0);
	}
	if (fn->ty->variadic) {
		if (!fn->va_area)
			fatal("internal error: missing va area");
		i_addi(T0, S0, -fn->va_area->offset);
		for (i = 0; i < 8; i++)
			i_sd(A0 + i, T0, i * 8);
		for (i = 0; i < 8; i++) {
			load_s0(T1, i * 8);
			i_sd(T1, T0, 64 + i * 8);
		}
	}
	for (i = 0; i < fn->ty->nparams; i++) {
		struct param *p = &fn->ty->params[i];
		struct obj *v = p->obj;
		int slot = i + (hidden ? 1 : 0);

		if (!v)
			fatal("internal error: missing parameter");
		i_addi(T0, S0, -v->offset);
		if (slot < 8) {
			i_sd(A0 + slot, T0, 0);
		} else {
			load_s0(T1, (slot - 8) * 8);
			i_sd(T1, T0, 0);
		}
	}
	for (i = 0; i < fn->ty->nparams; i++) {
		struct param *p = &fn->ty->params[i];
		struct obj *v = p->obj;

		if (!v)
			fatal("internal error: missing parameter");
		if (!is_aggregate(v->ty))
			continue;
		if (!memcpy_sym)
			fatal("internal error: missing memcpy runtime reference");
		i_addi(T0, S0, -v->offset);
		i_ld(A1, T0, 0);
		i_mv(A0, T0);
		i_li(A2, v->ty->size);
		i_call_sym(memcpy_sym);
	}
}

void gen_function(struct obj *fn)
{
	fn->sym->offset = code.len;
	clabels = NULL;
	emit_prologue(fn);
	gen_fn = fn;
	gen_stmt(fn->body);
	gen_fn = NULL;
	check_clabels();
	gen_default_return(fn);
	patch_fixups();
}

void gen_runtime(void)
{
	struct sym *s = new_sym("__cc_puts", SEC_TEXT, 0, 4);
	int l1 = new_label();
	int l2 = new_label();

	s->defined = true;
	s->offset = code.len;
	/* t0 = s, t1 = len */
	i_mv(T0, A0);
	i_li(T1, 0);
	place_label(l1);
	i_r(0, 0, T2, T0, T1); /* add t2,t0,t1 */
	i_lbu(28, T2, 0); /* t3 */
	jump_if_zero(28, l2);
	i_addi(T1, T1, 1);
	jump_to(l1);
	place_label(l2);
	/* write(1, s, len) */
	i_li(A0, 1);
	i_mv(A1, T0);
	i_mv(A2, T1);
	i_li(17, SYS_WRITE);
	i_ecall();
	/* write(1, "\n", 1) */
	i_addi(SP, SP, -16);
	i_li(T0, '\n');
	i_sb(T0, SP, 0);
	i_li(A0, 1);
	i_mv(A1, SP);
	i_li(A2, 1);
	i_li(17, SYS_WRITE);
	i_ecall();
	i_addi(SP, SP, 16);
	i_li(A0, 0);
	i_ret();
	patch_fixups();
	puts_sym = s;

	{
		struct sym *m = new_sym("__cc_memcpy", SEC_TEXT, 0, 4);
		int loop = new_label();
		int done = new_label();

		m->defined = true;
		m->offset = code.len;
		/* a0 = dst, a1 = src, a2 = n; returns dst in a0 */
		i_mv(T0, A0);
		jump_if_zero(A2, done);
		place_label(loop);
		i_lbu(T1, A1, 0);
		i_sb(T1, T0, 0);
		i_addi(A1, A1, 1);
		i_addi(T0, T0, 1);
		i_addi(A2, A2, -1);
		jump_if_nonzero(A2, loop);
		place_label(done);
		i_ret();
		patch_fixups();
		memcpy_sym = m;
	}

	{
		struct sym *m = new_sym("__cc_memset", SEC_TEXT, 0, 4);
		int loop = new_label();
		int done = new_label();

		m->defined = true;
		m->offset = code.len;
		/* a0 = dst, a1 = byte, a2 = n; returns dst in a0 */
		i_mv(T0, A0);
		jump_if_zero(A2, done);
		place_label(loop);
		i_sb(A1, T0, 0);
		i_addi(T0, T0, 1);
		i_addi(A2, A2, -1);
		jump_if_nonzero(A2, loop);
		place_label(done);
		i_ret();
		patch_fixups();
		memset_sym = m;
	}
}

void gen_start(struct sym *main_sym)
{
	static struct sym *start_sym;

	start_sym = new_sym("__cc_start", SEC_TEXT, 0, 4);
	start_sym->defined = true;
	start_sym->offset = code.len;
	i_call_sym(main_sym);
	i_li(17, SYS_EXIT);
	i_ecall();
	emit32(enc_j(0x6f, X0, 0));
}

static struct sym *new_runtime_ref(const char *name)
{
	struct sym *s = new_sym(name, SEC_TEXT, 0, 4);

	s->defined = false;
	return s;
}

void make_runtime_refs(void)
{
	puts_sym = new_runtime_ref("__cc_puts");
	memcpy_sym = new_runtime_ref("__cc_memcpy");
	memset_sym = new_runtime_ref("__cc_memset");
}

void reset_codegen_state(void)
{
	puts_sym = NULL;
	memcpy_sym = NULL;
	memset_sym = NULL;
	cur_break = -1;
	cur_cont = -1;
	gen_fn = NULL;
	clabels = NULL;
}
