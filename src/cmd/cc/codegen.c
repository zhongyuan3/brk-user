#include "cc.h"
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

static void cast_to(struct type *ty)
{
	if (ty->kind == TY_VOID || ty->kind == TY_PTR || ty->kind == TY_ARRAY ||
	    ty->kind == TY_FUNC)
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

static void load_value(struct type *ty)
{
	if (ty->kind == TY_ARRAY || ty->kind == TY_FUNC ||
	    ty->kind == TY_VOID || is_aggregate(ty))
		return;
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
	fatal("not an lvalue");
}

static struct sym *puts_sym;
static struct sym *memcpy_sym;
static struct sym *memset_sym;

static void gen_call(struct node *n)
{
	const char *name = n->var ? n->var->name : NULL;
	bool direct = n->var != NULL;
	bool builtin_ok = direct && !n->var->defined;
	int i;

	if (direct && !strcmp(name, "__builtin_va_start")) {
		int off;

		if (!gen_fn || !gen_fn->va_area)
			fatal("va_start used outside a variadic function");
		off = -(gen_fn->va_area->offset) + gen_fn->ty->nparams * 8;
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
		i_li(17, SYS_write); /* a7 = SYS_write */
		i_ecall();
		i_addi(SP, SP, 16);
		return;
	}
	if (builtin_ok && !strcmp(name, "puts")) {
		if (n->nargs != 1)
			fatal("puts expects one argument");
		gen_expr(n->args[0]);
		i_call_sym(puts_sym);
		return;
	}

	if (!direct) {
		gen_expr(n->callee);
		push_reg(A0);
	}
	for (i = 0; i < n->nargs; i++) {
		gen_expr(n->args[i]);
		push_reg(A0);
	}
	for (i = n->nargs - 1; i >= 0; i--)
		pop_reg(A0 + i);
	if (!direct) {
		pop_reg(T2);
		emit32(enc_i(0x67, RA, 0, T2, 0)); /* jalr ra,0(t2) */
		if (n->ty && n->ty->size < 8)
			cast_to(n->ty);
		return;
	}

	if (builtin_ok && !strcmp(name, "exit")) {
		if (n->nargs == 0)
			i_li(A0, 0);
		i_li(17, SYS_exit); /* a7 = SYS_exit */
		i_ecall();
		emit32(enc_j(0x6f, X0, 0));
		return;
	}

	if (builtin_ok && (!strcmp(name, "write") || !strcmp(name, "read") ||
			   !strcmp(name, "close"))) {
		int sysno;

		if (!strcmp(name, "write"))
			sysno = SYS_write;
		else if (!strcmp(name, "read"))
			sysno = SYS_read;
		else
			sysno = SYS_close;
		i_li(17, sysno);
		i_ecall();
		return;
	}

	i_call_sym(n->var->sym);
	{
		struct type *rt = n->ty;

		/* The call may have been parsed before the function definition. */
		if (n->var->ty && n->var->ty->kind == TY_FUNC &&
		    n->var->ty->base)
			rt = n->var->ty->base;
		if (rt && rt->size < 8)
			cast_to(rt);
	}
}

static void gen_bin(struct node *n)
{
	struct type *lt = decay(n->lhs->ty);
	struct type *rt = decay(n->rhs->ty);
	int unsigned_cmp = lt->is_unsigned || rt->is_unsigned ||
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
	gen_addr(n->lhs);
	push_reg(A0);
	load_value(n->lhs->ty);
	push_reg(A0);
	gen_expr(n->rhs);
	i_mv(A1, A0);
	pop_reg(A0);
	switch (n->val) {
	case ND_ADD:
		if (is_ptr_like(n->lhs->ty)) {
			int step = type_step(n->lhs->ty);

			if (step != 1) {
				i_li(T0, step);
				i_r(1, 0, A1, A1, T0);
			}
		}
		i_r(0, 0, A0, A0, A1);
		break;
	case ND_SUB:
		if (is_ptr_like(n->lhs->ty)) {
			int step = type_step(n->lhs->ty);

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
		i_r(1, 4, A0, A0, A1);
		break;
	case ND_MOD:
		i_r(1, 6, A0, A0, A1);
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
		i_r(n->lhs->ty->is_unsigned ? 0 : 0x20, 5, A0, A0, A1);
		break;
	default:
		fatal("internal error: bad compound assignment");
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
	int step = type_step(n->lhs->ty);
	int delta = (int)n->val;
	int prefix = n->kind == ND_PREINC;

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

static void gen_expr(struct node *n)
{
	switch (n->kind) {
	case ND_NULL:
		return;
	case ND_NUM:
		i_li(A0, n->val);
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
			i_call_sym(memcpy_sym);
			return;
		}
		gen_addr(n->lhs);
		push_reg(A0);
		gen_expr(n->rhs);
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
		cast_to(n->ty);
		return;
	case ND_COMMA:
		gen_expr(n->lhs);
		gen_expr(n->rhs);
		return;
	case ND_COND: {
		int lelse = new_label();
		int lend = new_label();

		gen_expr(n->cond);
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

		gen_expr(n->lhs);
		jump_if_zero(A0, lfalse);
		gen_expr(n->rhs);
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

		gen_expr(n->lhs);
		jump_if_nonzero(A0, ltrue);
		gen_expr(n->rhs);
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
		i_r(0x20, 0, A0, X0, A0);
		cast_to(n->ty);
		return;
	case ND_NOT:
		gen_expr(n->lhs);
		i_seqz(A0, A0);
		return;
	case ND_BITNOT:
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
		if (n->lhs)
			gen_expr(n->lhs);
		else
			i_li(A0, 0);
		i_mv(SP, S0);
		i_ld(RA, SP, -8);
		i_ld(S0, SP, -16);
		i_ret();
		return;
	case ND_IF: {
		int lelse = new_label();
		int lend = new_label();

		gen_expr(n->cond);
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
		gen_expr(n->cond);
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
			gen_expr(n->cond);
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
		gen_expr(n->cond);
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
	int i;

	/* Preserve the caller's s0 before s0 becomes the new frame pointer. */
	i_mv(T0, S0);
	i_addi(SP, SP, -f);
	i_addi(S0, SP, f);
	i_sd(RA, S0, -8);
	i_sd(T0, S0, -16);
	for (i = 0; i < fn->ty->nparams; i++) {
		struct param *p = &fn->ty->params[i];
		struct obj *v = p->obj;

		if (!v)
			fatal("internal error: missing parameter");
		i_addi(T0, S0, -v->offset);
		i_sd(A0 + i, T0, 0);
	}
	if (fn->ty->variadic) {
		if (!fn->va_area)
			fatal("internal error: missing va area");
		i_addi(T0, S0, -fn->va_area->offset);
		for (i = 0; i < 8; i++)
			i_sd(A0 + i, T0, i * 8);
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
	i_li(A0, 0);
	i_mv(SP, S0);
	i_ld(RA, SP, -8);
	i_ld(S0, SP, -16);
	i_ret();
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
	i_li(17, SYS_write);
	i_ecall();
	/* write(1, "\n", 1) */
	i_addi(SP, SP, -16);
	i_li(T0, '\n');
	i_sb(T0, SP, 0);
	i_li(A0, 1);
	i_mv(A1, SP);
	i_li(A2, 1);
	i_li(17, SYS_write);
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
	i_li(17, SYS_exit);
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
}
