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

static void i_flw(int fd, int rs1, int imm)
{
	emit32(enc_i(0x07, fd, 2, rs1, imm));
}

static void i_fld(int fd, int rs1, int imm)
{
	emit32(enc_i(0x07, fd, 3, rs1, imm));
}

static void i_fsw(int fs2, int rs1, int imm)
{
	emit32(enc_s(0x27, 2, rs1, fs2, imm));
}

static void i_fsd(int fs2, int rs1, int imm)
{
	emit32(enc_s(0x27, 3, rs1, fs2, imm));
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

/* ------------------------------------------------------------------ */
/* RISC-V LP64D argument classification                                */
/* ------------------------------------------------------------------ */

/*
 * The first two eight-byte words of a small aggregate are classified
 * independently.  GCC's RISC-V psABI uses per-field registers for a
 * two-field aggregate (including mixed float/int), packs integer-only
 * aggregates by eightbyte, and falls back to the integer convention for
 * aggregates with more than two leaf fields.  Aggregates larger than
 * 2*XLEN, or with unaligned/oversized fields, are passed by reference.
 */

struct abi_leaf {
	int offset;
	int size;
	bool fp;
};

struct abi_word {
	bool fp;
	int offset;
	int size;
};

struct abi_class {
	bool mem;
	int nwords;
	struct abi_word word[2];
};

static bool abi_collect_leaves(struct type *ty, int off,
			       struct abi_leaf *leaf, int *n, int cap,
			       bool *bad)
{
	int i;

	if (is_aggregate(ty)) {
		if (!ty->is_complete || ty->size <= 0 || ty->size > 16 ||
		    ty->align > 8)
			*bad = true;
		if (ty->kind == TY_UNION) {
			/*
			 * Unions overlap; use the conservative integer
			 * convention for multi-member unions.  A single-member
			 * union still classifies like that member.
			 */
			if (ty->nmembers != 1)
				return false;
			return abi_collect_leaves(ty->members[0].ty, off,
						  leaf, n, cap, bad);
		}
		for (i = 0; i < ty->nmembers; i++) {
			struct member *m = &ty->members[i];

			if (!m->name && m->bit_width == 0)
				continue;
			if (!abi_collect_leaves(m->ty, off + m->offset,
						leaf, n, cap, bad))
				return false;
		}
		return true;
	}
	if (ty->kind == TY_ARRAY) {
		if (ty->array_len < 0 || ty->base->size <= 0)
			*bad = true;
		for (i = 0; i < ty->array_len; i++) {
			if (!abi_collect_leaves(ty->base,
						off + i * ty->base->size,
						leaf, n, cap, bad))
				return false;
		}
		return true;
	}
	if (ty->kind == TY_FUNC || ty->kind == TY_VOID || ty->size <= 0 ||
	    ty->size > 8 || ty->align > 8 || (off % ty->size) != 0) {
		*bad = true;
		return true;
	}
	if (*n >= cap)
		return false;
	leaf[*n].offset = off;
	leaf[*n].size = ty->size;
	leaf[*n].fp = is_float(ty);
	(*n)++;
	return true;
}

static void abi_integer_words(struct abi_class *c, int size)
{
	int i;
	int left = size;

	c->mem = false;
	c->nwords = (size + 7) / 8;
	for (i = 0; i < c->nwords; i++) {
		int n = left > 8 ? 8 : left;

		c->word[i].fp = false;
		c->word[i].offset = i * 8;
		c->word[i].size = n;
		left -= n;
	}
}

static void classify_aggregate(struct type *ty, struct abi_class *c)
{
	struct abi_leaf leaf[3];
	int n = 0;
	int fp_count = 0;
	int i;
	bool bad = false;
	bool overflow;

	memset(c, 0, sizeof(*c));
	if (!ty->is_complete || ty->size <= 0 || ty->size > 16 ||
	    ty->align > 8 || ty->kind == TY_UNION) {
		if (ty->kind == TY_UNION && ty->size > 0 && ty->size <= 16 &&
		    ty->align <= 8)
			abi_integer_words(c, ty->size);
		else
			c->mem = true;
		return;
	}
	overflow = !abi_collect_leaves(ty, 0, leaf, &n,
				       (int)(sizeof(leaf) / sizeof(leaf[0])),
				       &bad);
	if (bad || n == 0) {
		c->mem = true;
		return;
	}
	for (i = 0; i < n; i++)
		if (leaf[i].fp)
			fp_count++;

	/*
	 * More than two leaf fields use the integer convention.  This is
	 * what GCC does for {float,float,float}, float arrays, and large
	 * integer arrays.
	 */
	if (overflow || n > 2) {
		abi_integer_words(c, ty->size);
		return;
	}
	if (fp_count == 0) {
		abi_integer_words(c, ty->size);
		return;
	}
	if (fp_count == n) {
		c->nwords = n;
		for (i = 0; i < n; i++) {
			c->word[i].fp = true;
			c->word[i].offset = leaf[i].offset;
			c->word[i].size = leaf[i].size;
		}
		return;
	}
	/* Exactly two mixed fields, e.g. {float,int} or {char,double}. */
	if (n != 2) {
		abi_integer_words(c, ty->size);
		return;
	}
	c->nwords = 2;
	for (i = 0; i < 2; i++) {
		c->word[i].fp = leaf[i].fp;
		c->word[i].offset = leaf[i].offset;
		c->word[i].size = leaf[i].size;
	}
}

static void classify_type(struct type *ty, bool fp_allowed,
			  struct abi_class *c)
{
	memset(c, 0, sizeof(*c));
	if (is_aggregate(ty)) {
		classify_aggregate(ty, c);
		return;
	}
	c->nwords = 1;
	c->word[0].offset = 0;
	c->word[0].size = ty->size > 0 ? ty->size : 8;
	c->word[0].fp = fp_allowed && is_float(ty);
}

struct abi_word_assign {
	int reg;      /* register number, or -1 */
	int reg_slot; /* temporary outgoing slot, or -1 */
	int stack_off;
	bool stack;
};

struct abi_value {
	struct abi_class cls;
	bool mem;        /* aggregate passed/returned by reference */
	bool agg;        /* value is a struct/union */
	bool fp_allowed; /* named argument or return value */
	int size;        /* size of the original type in bytes */
	int nwords;
	struct abi_word_assign w[2];
};

static void classify_value(struct type *ty, bool fp_allowed,
			   struct abi_value *v)
{
	struct abi_class raw;

	memset(v, 0, sizeof(*v));
	classify_type(ty, fp_allowed, &raw);
	v->mem = is_aggregate(ty) && raw.mem;
	v->agg = is_aggregate(ty);
	v->fp_allowed = fp_allowed;
	v->size = ty->size;
	if (v->mem) {
		raw.mem = false;
		raw.nwords = 1;
		raw.word[0].fp = false;
		raw.word[0].offset = 0;
		raw.word[0].size = 8;
	}
	v->cls = raw;
	v->nwords = raw.nwords;
}

static void abi_count(struct abi_value *v, int *need_i, int *need_f)
{
	int i;

	*need_i = 0;
	*need_f = 0;
	for (i = 0; i < v->nwords; i++) {
		if (v->cls.word[i].fp)
			(*need_f)++;
		else
			(*need_i)++;
	}
}

static void abi_assign_regs(struct abi_value *v, int *ir, int *fr,
			    int *rs)
{
	int i;

	for (i = 0; i < v->nwords; i++) {
		v->w[i].stack = false;
		v->w[i].stack_off = -1;
		v->w[i].reg_slot = (*rs)++;
		if (v->cls.word[i].fp)
			v->w[i].reg = (*fr)++;
		else
			v->w[i].reg = (*ir)++;
	}
}

static void abi_assign_stack(struct abi_value *v, int *stk, int from)
{
	int i;

	for (i = from; i < v->nwords; i++) {
		v->w[i].stack = true;
		v->w[i].stack_off = (*stk)++;
		v->w[i].reg = -1;
		v->w[i].reg_slot = -1;
	}
}

/*
 * Assign one argument (or return value) to registers and/or the stack.
 *
 * The hardware floating-point convention says a named floating-point value
 * is passed in an FP register "if at least one floating-point argument
 * register is available.  Otherwise, it is passed according to the integer
 * calling convention."  A multiword value needs its whole FP register
 * group, so a two-FP struct with only one FP register left also falls back
 * to the integer convention.  The integer convention may split a two-word
 * aggregate when only one integer register remains: the first word goes in
 * the register, the rest on the stack.
 */
static void abi_assign(struct abi_value *v, int *ir, int *fr, int *stk,
		       int *rs)
{
	int need_i;
	int need_f;
	int i;

	abi_count(v, &need_i, &need_f);
	if (*ir + need_i <= 8 && *fr + need_f <= 8) {
		abi_assign_regs(v, ir, fr, rs);
		return;
	}

	if (v->fp_allowed && need_f > 0) {
		if (v->agg)
			abi_integer_words(&v->cls, v->size);
		else {
			v->cls.nwords = 1;
			v->cls.word[0].fp = false;
			v->cls.word[0].offset = 0;
			v->cls.word[0].size = v->size > 0 ? v->size : 8;
		}
		v->nwords = v->cls.nwords;
		v->fp_allowed = false;
		abi_count(v, &need_i, &need_f);
		if (*ir + need_i <= 8) {
			abi_assign_regs(v, ir, fr, rs);
			return;
		}
	}

	if (need_f == 0 && *ir < 8) {
		int avail = 8 - *ir;

		if (avail > v->nwords)
			avail = v->nwords;
		if (avail > 0 && v->agg) {
			/* Split an aggregate across the boundary. */
			for (i = 0; i < avail; i++) {
				v->w[i].stack = false;
				v->w[i].stack_off = -1;
				v->w[i].reg_slot = (*rs)++;
				v->w[i].reg = (*ir)++;
			}
			abi_assign_stack(v, stk, avail);
			return;
		}
	}

	for (i = 0; i < v->nwords; i++) {
		v->w[i].stack = true;
		v->w[i].stack_off = (*stk)++;
		v->w[i].reg = -1;
		v->w[i].reg_slot = -1;
	}
}

static void mem_base(int rd, int base, int off)
{
	if (off >= -2048 && off <= 2047) {
		i_addi(rd, base, off);
		return;
	}
	i_li(T6, off);
	i_r(0, 0, rd, base, T6);
}

static void load_word_size(int rd, int base, int off, int size)
{
	mem_base(T6, base, off);
	switch (size) {
	case 1:
		i_lbu(rd, T6, 0);
		break;
	case 2:
		i_lhu(rd, T6, 0);
		break;
	case 4:
		i_lwu(rd, T6, 0);
		break;
	case 8:
		i_ld(rd, T6, 0);
		break;
	default:
		fatal("internal error: bad ABI word size");
	}
}

static void store_word_size(int reg, int base, int off, int size)
{
	mem_base(T6, base, off);
	switch (size) {
	case 1:
		i_sb(reg, T6, 0);
		break;
	case 2:
		i_sh(reg, T6, 0);
		break;
	case 4:
		i_sw(reg, T6, 0);
		break;
	case 8:
		i_sd(reg, T6, 0);
		break;
	default:
		fatal("internal error: bad ABI word size");
	}
}

static void load_fp_size(int fd, int base, int off, int size)
{
	if (size == 4)
		i_flw(fd, base, off);
	else if (size == 8)
		i_fld(fd, base, off);
	else
		fatal("internal error: bad FP ABI word size");
}

static void store_fp_size(int fs, int base, int off, int size)
{
	if (size == 4)
		i_fsw(fs, base, off);
	else if (size == 8)
		i_fsd(fs, base, off);
	else
		fatal("internal error: bad FP ABI word size");
}

static int va_start_offset(struct obj *fn)
{
	int ir = 0;
	int fr = 0;
	int stk = 0;
	int rs = 0;
	int i;
	struct type *rt = fn->ty->base;

	if (is_aggregate(rt)) {
		struct abi_value rv;

		classify_value(rt, true, &rv);
		if (rv.mem)
			ir = 1; /* hidden result pointer */
	}
	for (i = 0; i < fn->ty->nparams; i++) {
		struct abi_value v;

		classify_value(fn->ty->params[i].ty, true, &v);
		abi_assign(&v, &ir, &fr, &stk, &rs);
	}
	if (ir < 8)
		return ir * 8;
	return 64 + stk * 8;
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
static bool hosted_mode;

void set_hosted_mode(bool hosted)
{
	hosted_mode = hosted;
}

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
	bool builtin_ok = direct && !n->var->defined && !hosted_mode;
	bool indirect = n->callee != NULL;
	struct type *rt = n->ty;
	struct type *fnty = NULL;
	bool varargs = false;
	bool hidden = false;
	struct abi_value retv;
	struct abi_value hiddenv;
	struct abi_value *av = NULL;
	int nparams = 0;
	int ir = 0;
	int fr = 0;
	int stk = 0;
	int rs = 0;
	int reg_words;
	int stack_words;
	int total_slots;
	int total;
	int int_slot[8];
	int fp_slot[8];
	int fp_size[8];
	int indirect_slot;
	int sysno = -1;
	int i;
	int j;

	if (direct && !strcmp(name, "__builtin_va_start")) {
		int off;

		if (!gen_fn || !gen_fn->ty->variadic)
			fatal("va_start used outside a variadic function");
		off = va_start_offset(gen_fn);
		if (n->nargs > 0) {
			gen_addr(n->args[0]);
			i_mv(T2, A0);
			i_addi(A0, S0, off);
			i_sd(A0, T2, 0);
		} else {
			i_addi(A0, S0, off);
		}
		return;
	}

	if (direct && !strcmp(name, "__builtin_va_end")) {
		i_li(A0, 0);
		return;
	}
	if (direct && !strcmp(name, "__builtin_va_copy")) {
		if (n->nargs != 2)
			fatal("va_copy expects two arguments");
		gen_expr(n->args[1]);
		i_mv(T0, A0);
		gen_addr(n->args[0]);
		i_sd(T0, A0, 0);
		i_mv(A0, T0);
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

	if (n->var && n->var->ty && n->var->ty->kind == TY_FUNC)
		fnty = n->var->ty;
	else if (n->callee) {
		struct type *ct = decay(n->callee->ty);

		if (ct->kind == TY_PTR && ct->base->kind == TY_FUNC)
			fnty = ct->base;
	}
	if (fnty) {
		nparams = fnty->nparams;
		varargs = fnty->variadic;
		if (fnty->base)
			rt = fnty->base;
	}
	if (!rt)
		rt = ty_int;

	if (is_aggregate(rt)) {
		classify_value(rt, true, &retv);
		hidden = retv.mem;
		if (!hidden && !n->ret_tmp)
			fatal("internal error: missing aggregate-return temporary");
		if (hidden && !n->ret_tmp)
			fatal("internal error: missing aggregate-return temporary");
	}

	memset(int_slot, -1, sizeof(int_slot));
	memset(fp_slot, -1, sizeof(fp_slot));
	memset(fp_size, 0, sizeof(fp_size));

	av = xcalloc((size_t)(n->nargs > 0 ? n->nargs : 1), sizeof(*av));

	/* Hidden result pointer is always the first integer argument. */
	memset(&hiddenv, 0, sizeof(hiddenv));
	if (hidden) {
		hiddenv.nwords = 1;
		hiddenv.cls.nwords = 1;
		hiddenv.cls.word[0].size = 8;
		hiddenv.cls.word[0].fp = false;
		hiddenv.size = 8;
		abi_assign(&hiddenv, &ir, &fr, &stk, &rs);
		if (hiddenv.w[0].reg < 0)
			fatal("internal error: hidden result pointer on stack");
	}

	for (i = 0; i < n->nargs; i++) {
		struct node *arg = n->args[i];
		bool fp_allowed = !(varargs && i >= nparams);

		classify_value(arg->ty, fp_allowed, &av[i]);
		abi_assign(&av[i], &ir, &fr, &stk, &rs);
	}
	reg_words = rs;
	stack_words = stk;

	if (hidden)
		int_slot[hiddenv.w[0].reg] =
			stack_words + hiddenv.w[0].reg_slot;
	for (i = 0; i < n->nargs; i++) {
		for (j = 0; j < av[i].nwords; j++) {
			if (!av[i].w[j].stack) {
				if (av[i].cls.word[j].fp) {
					fp_slot[av[i].w[j].reg] =
						stack_words + av[i].w[j].reg_slot;
					fp_size[av[i].w[j].reg] =
						av[i].cls.word[j].size;
				} else {
					int_slot[av[i].w[j].reg] =
						stack_words + av[i].w[j].reg_slot;
				}
			}
		}
	}
	indirect_slot = stack_words + reg_words;
	total_slots = indirect_slot + (indirect ? 1 : 0);
	total = align_to(total_slots * 8, 16);

	push_reg(S1);
	if (total)
		i_addi(SP, SP, -total);
	i_mv(S1, SP);

	if (hidden) {
		int off = stack_words + hiddenv.w[0].reg_slot;

		i_addi(A0, S0, -n->ret_tmp->offset);
		store_word_size(A0, S1, off * 8, 8);
	}
	if (indirect) {
		gen_expr(n->callee);
		store_word_size(A0, S1, indirect_slot * 8, 8);
	}

	for (i = n->nargs - 1; i >= 0; i--) {
		struct node *arg = n->args[i];

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
			if (av[i].mem) {
				int off;

				if (av[i].w[0].stack)
					off = av[i].w[0].stack_off * 8;
				else
					off = (stack_words +
					       av[i].w[0].reg_slot) *
					      8;
				i_addi(A0, S0, -tmp->offset);
				store_word_size(A0, S1, off, 8);
			} else {
				for (j = 0; j < av[i].nwords; j++) {
					int off;
					struct abi_word *w = &av[i].cls.word[j];

					load_word_size(T0, S0,
						       -tmp->offset + w->offset,
						       w->size);
					if (av[i].w[j].stack)
						off = av[i].w[j].stack_off * 8;
					else
						off = (stack_words +
						       av[i].w[j].reg_slot) *
						      8;
					store_word_size(T0, S1, off, w->size);
				}
			}
		} else {
			int off;
			struct abi_word *w = &av[i].cls.word[0];
			int size = w->size;

			gen_expr(arg);
			/*
			 * Scalar integers/pointers occupy a full XLEN register in
			 * the standard ABI.  Fill the whole outgoing slot so a
			 * syscall or an unprototyped callee cannot observe stale
			 * upper bits.
			 */
			if (!w->fp && !is_float(arg->ty))
				size = 8;
			if (av[i].w[0].stack)
				off = av[i].w[0].stack_off * 8;
			else
				off = (stack_words + av[i].w[0].reg_slot) * 8;
			store_word_size(A0, S1, off, size);
		}
	}

	for (i = 0; i < 8; i++) {
		if (int_slot[i] >= 0)
			i_ld(A0 + i, S1, int_slot[i] * 8);
		if (fp_slot[i] >= 0)
			load_fp_size(10 + i, S1, fp_slot[i] * 8, fp_size[i]);
	}
	if (indirect)
		load_s1(T2, indirect_slot * 8);

	i_mv(SP, S1);
	if (sysno == SYS_OPEN) {
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

	if (is_aggregate(rt)) {
		if (hidden) {
			i_addi(A0, S0, -n->ret_tmp->offset);
			return;
		}
		{
			int rir = 0;
			int rfr = 0;
			int rstk = 0;
			int rrs = 0;

			abi_assign(&retv, &rir, &rfr, &rstk, &rrs);
			i_addi(T0, S0, -n->ret_tmp->offset);
			for (j = 0; j < retv.nwords; j++) {
				struct abi_word *w = &retv.cls.word[j];

				if (retv.w[j].stack)
					fatal("internal error: aggregate return spilled");
				if (w->fp)
					store_fp_size(10 + retv.w[j].reg, T0,
						      w->offset, w->size);
				else
					store_word_size(A0 + retv.w[j].reg, T0,
							w->offset, w->size);
			}
			i_addi(A0, S0, -n->ret_tmp->offset);
			return;
		}
	}
	if (rt->kind == TY_FLOAT || rt->kind == TY_DOUBLE) {
		fp_to_int(A0, 10, rt->kind == TY_DOUBLE);
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

static void gen_va_arg(struct node *n)
{
	int align = n->ty->align > 8 ? n->ty->align : 8;
	int step = align_to(n->ty->size > 8 ? n->ty->size : 8, align);

	gen_addr(n->lhs);
	i_mv(T2, A0);            /* address of the va_list object */
	i_ld(T0, T2, 0);         /* current argument pointer */
	if (align > 8) {
		i_addi(T1, T0, align - 1);
		i_li(T6, -align);
		i_r(0, 7, T0, T1, T6); /* and */
	}
	i_addi(T1, T0, step);
	i_sd(T1, T2, 0);         /* advance ap */
	if (is_aggregate(n->ty)) {
		i_mv(A0, T0);
		return;
	}
	i_mv(A0, T0);
	load_value(n->ty);
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
	case ND_VA_ARG:
		gen_va_arg(n);
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
	/* s0 = old_sp - 64; the caller's stack starts at s0 + 64. */
	i_addi(SP, S0, 64);
	i_ld(RA, S0, -8);
	i_ld(S1, S0, -24);
	i_ld(S0, S0, -16);
	i_ret();
}

static void gen_default_return(struct obj *fn)
{
	struct type *rt = fn->ty->base;

	if (is_aggregate(rt)) {
		struct abi_value rv;

		classify_value(rt, true, &rv);
		if (rv.mem) {
			if (!fn->ret_ptr)
				fatal("internal error: missing aggregate-return pointer");
			i_addi(T0, S0, -fn->ret_ptr->offset);
			i_ld(A0, T0, 0);
		} else {
			int ir = 0;
			int fr = 0;
			int stk = 0;
			int rs = 0;
			int i;

			abi_assign(&rv, &ir, &fr, &stk, &rs);
			for (i = 0; i < rv.nwords; i++) {
				if (rv.w[i].stack)
					continue;
				if (rv.cls.word[i].fp)
					fp_const(10 + rv.w[i].reg, 0,
						 rv.cls.word[i].size == 8);
				else
					i_li(A0 + rv.w[i].reg, 0);
			}
		}
	} else if (rt->kind == TY_FLOAT || rt->kind == TY_DOUBLE) {
		i_li(A0, 0);
		fp_from_int(10, A0, rt->kind == TY_DOUBLE);
	} else {
		i_li(A0, 0);
	}
	gen_epilogue();
}

static void gen_return(struct node *n)
{
	struct type *rt = gen_fn->ty->base;

	if (n->lhs && is_aggregate(rt)) {
		struct abi_value rv;
		int i;

		classify_value(rt, true, &rv);
		if (rv.mem) {
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
		gen_expr(n->lhs); /* source address in a0 */
		i_mv(T0, A0);
		{
			int ir = 0;
			int fr = 0;
			int stk = 0;
			int rs = 0;

			abi_assign(&rv, &ir, &fr, &stk, &rs);
		}
		for (i = 0; i < rv.nwords; i++) {
			struct abi_word *w = &rv.cls.word[i];

			if (rv.w[i].stack)
				fatal("internal error: aggregate return spilled");
			if (w->fp)
				load_fp_size(10 + rv.w[i].reg, T0, w->offset,
					     w->size);
			else
				load_word_size(A0 + rv.w[i].reg, T0, w->offset,
					       w->size);
		}
		gen_epilogue();
		return;
	}
	if (n->lhs) {
		gen_expr(n->lhs);
		if (rt->kind == TY_FLOAT || rt->kind == TY_DOUBLE) {
			fp_from_int(10, A0, rt->kind == TY_DOUBLE);
		} else if (rt->kind == TY_VOID) {
			i_li(A0, 0);
		} else {
			cast_convert(n->lhs->ty, rt);
		}
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
	struct abi_value *pv;
	bool *copy;
	struct abi_value rv;
	bool hidden = false;
	int ir = 0;
	int fr = 0;
	int stk = 0;
	int rs = 0;
	int f;
	int i;
	int j;

	memset(&rv, 0, sizeof(rv));
	if (is_aggregate(fn->ty->base)) {
		classify_value(fn->ty->base, true, &rv);
		hidden = rv.mem;
	}
	f = align_to(fn->frame_size + 64, 16);
	if (f < 96)
		f = 96;

	/* Preserve the caller's s0 before s0 becomes the new frame pointer. */
	i_mv(T0, S0);
	i_addi(SP, SP, -f);
	i_addi(S0, SP, f - 64);
	i_sd(RA, S0, -8);
	i_sd(T0, S0, -16);
	i_sd(S1, S0, -24);

	/*
	 * The 64 bytes at s0+0..s0+63 are the va_list register save area.
	 * The caller's stack arguments start at s0+64 (the old sp).
	 */
	if (fn->ty->variadic) {
		for (i = 0; i < 8; i++)
			i_sd(A0 + i, S0, i * 8);
	}

	pv = xcalloc((size_t)(fn->ty->nparams > 0 ? fn->ty->nparams : 1),
		     sizeof(*pv));
	copy = xcalloc((size_t)(fn->ty->nparams > 0 ? fn->ty->nparams : 1),
		       sizeof(*copy));

	if (hidden) {
		if (!fn->ret_ptr)
			fatal("internal error: missing aggregate-return pointer");
		i_addi(T0, S0, -fn->ret_ptr->offset);
		i_sd(A0, T0, 0);
		ir = 1;
	}

	for (i = 0; i < fn->ty->nparams; i++) {
		struct param *p = &fn->ty->params[i];

		if (!p->obj)
			fatal("internal error: missing parameter");
		classify_value(p->ty, true, &pv[i]);
		abi_assign(&pv[i], &ir, &fr, &stk, &rs);
	}

	for (i = 0; i < fn->ty->nparams; i++) {
		struct obj *v = fn->ty->params[i].obj;

		if (pv[i].mem) {
			struct abi_word_assign *w = &pv[i].w[0];

			if (w->stack)
				load_word_size(T0, S0, 64 + w->stack_off * 8, 8);
			else
				i_mv(T0, A0 + w->reg);
			store_word_size(T0, S0, -v->offset, 8);
			copy[i] = true;
			continue;
		}
		for (j = 0; j < pv[i].nwords; j++) {
			struct abi_word *w = &pv[i].cls.word[j];
			struct abi_word_assign *a = &pv[i].w[j];
			int off = -v->offset + w->offset;

			if (a->stack) {
				load_word_size(T0, S0, 64 + a->stack_off * 8,
					       w->size);
				store_word_size(T0, S0, off, w->size);
			} else if (w->fp) {
				store_fp_size(10 + a->reg, S0, off, w->size);
			} else {
				store_word_size(A0 + a->reg, S0, off, w->size);
			}
		}
	}

	/* Copy by-reference aggregate parameters into their local objects. */
	for (i = 0; i < fn->ty->nparams; i++) {
		struct obj *v = fn->ty->params[i].obj;

		if (!copy[i])
			continue;
		if (!memcpy_sym)
			fatal("internal error: missing memcpy runtime reference");
		i_addi(T0, S0, -v->offset);
		i_ld(A1, T0, 0);
		i_mv(A0, T0);
		i_li(A2, v->ty->size);
		i_call_sym(memcpy_sym);
	}

	free(pv);
	free(copy);
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
