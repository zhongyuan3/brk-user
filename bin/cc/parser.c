#include "brkcc.h"

/* ------------------------------------------------------------------ */
/* parser state                                                        */
/* ------------------------------------------------------------------ */

static int tok_pos;

struct switch_ctx {
	struct case_entry *cases;
	struct switch_ctx *prev;
};

static struct switch_ctx *switches;

/*
 * Calls to functions that are not defined yet are remembered so that a
 * later prototype or definition can give them their real type and argument
 * conversions.  A call made through an implicit declaration whose real
 * return type is floating point or an aggregate is rejected: the enclosing
 * expression was already built assuming an int result and cannot be
 * corrected safely.
 */
struct fn_state {
	struct obj *fn;
	bool implicit;
	struct fn_state *next;
};

struct pending_call {
	struct node *call;
	struct obj *fn;
	bool implicit;
	struct pending_call *next;
};

static struct fn_state *fn_states;
static struct pending_call *pending_calls;

static struct fn_state *fn_state_for(struct obj *fn)
{
	struct fn_state *s;

	for (s = fn_states; s; s = s->next) {
		if (s->fn == fn)
			return s;
	}
	s = xcalloc(1, sizeof(*s));
	s->fn = fn;
	s->next = fn_states;
	fn_states = s;
	return s;
}

static struct token *cur_token(void)
{
	return &tokens[tok_pos];
}

static bool is(const char *s)
{
	struct token *t = cur_token();

	return t->text && !strcmp(t->text, s);
}

static bool is_kind(int kind)
{
	return cur_token()->kind == kind;
}

static bool consume(const char *s)
{
	if (is(s)) {
		tok_pos++;
		return true;
	}
	return false;
}

static void expect(const char *s)
{
	if (!consume(s))
		fatal("%d:%d: expected '%s' near '%s'", cur_token()->line,
		      cur_token()->col, s, cur_token()->text);
}

static void skip_attributes(void)
{
	while (is("__attribute__") || is("__attribute")) {
		int depth;

		tok_pos++;
		if (!consume("("))
			continue;
		depth = 1;
		while (depth > 0) {
			if (is_kind(TK_EOF))
				fatal("unterminated __attribute__");
			if (consume("("))
				depth++;
			else if (consume(")"))
				depth--;
			else
				tok_pos++;
		}
	}
}

/* ---- typedef, tag and enumerator namespaces ---- */

struct typedef_sym {
	char *name;
	struct type *ty;
	struct typedef_sym *next;
};

struct tag_sym {
	char *name;
	int kind;
	struct type *ty;
	struct tag_sym *next;
};

struct const_sym {
	char *name;
	long val;
	struct const_sym *next;
};

enum {
	TAG_STRUCT = 0,
	TAG_UNION,
	TAG_ENUM,
};

static struct typedef_sym *typedefs;
static struct tag_sym *tags;
static struct const_sym *consts;

static struct type *find_typedef(const char *name)
{
	struct typedef_sym *t;

	for (t = typedefs; t; t = t->next) {
		if (!strcmp(t->name, name))
			return t->ty;
	}
	return NULL;
}

static void add_typedef(const char *name, struct type *ty)
{
	struct typedef_sym *t;

	for (t = typedefs; t; t = t->next) {
		if (!strcmp(t->name, name)) {
			t->ty = ty;
			return;
		}
	}
	t = xcalloc(1, sizeof(*t));
	t->name = xstrdup(name);
	t->ty = ty;
	t->next = typedefs;
	typedefs = t;
}

static struct type *find_tag(int kind, const char *name)
{
	struct tag_sym *t;
	struct type *ty;

	for (t = tags; t; t = t->next) {
		if (!strcmp(t->name, name)) {
			if (t->kind != kind)
				fatal("tag '%s' redeclared as a different kind",
				      name);
			return t->ty;
		}
	}
	if (kind == TAG_ENUM) {
		ty = ty_int;
	} else {
		ty = new_aggregate_type(
			kind == TAG_STRUCT ? TY_STRUCT : TY_UNION, name);
	}
	t = xcalloc(1, sizeof(*t));
	t->name = xstrdup(name);
	t->kind = kind;
	t->ty = ty;
	t->next = tags;
	tags = t;
	return ty;
}

static bool find_const(const char *name, long *out)
{
	struct const_sym *c;

	for (c = consts; c; c = c->next) {
		if (!strcmp(c->name, name)) {
			if (out)
				*out = c->val;
			return true;
		}
	}
	return false;
}

static void add_const(const char *name, long val)
{
	struct const_sym *c;

	if (find_const(name, NULL))
		fatal("redefinition of enumerator '%s'", name);
	c = xcalloc(1, sizeof(*c));
	c->name = xstrdup(name);
	c->val = val;
	c->next = consts;
	consts = c;
}

static bool is_typename_at(int pos)
{
	static const char *const names[] = {
		"void",	  "char",   "short", "int",	"long",	   "float",
		"double", "unsigned", "signed", "const", "volatile", "static",
		"extern", "register", "inline", "struct", "union",   "enum",
		"typedef", "__builtin_va_list",
	};

	if (pos < 0 || pos >= ntokens || tokens[pos].kind != TK_IDENT)
		return false;
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		if (!strcmp(tokens[pos].text, names[i]))
			return true;
	}
	return find_typedef(tokens[pos].text) != NULL;
}

static bool is_typename(void)
{
	return is_typename_at(tok_pos);
}

/* ------------------------------------------------------------------ */
/* forward declarations                                                */
/* ------------------------------------------------------------------ */

static struct node *parse_expr(void);
static struct node *parse_assign(void);
static struct node *parse_cond(void);
static struct node *parse_stmt(void);
static struct node *parse_block(void);
static long eval_const(struct node *n);
static struct type *parse_declspec(void);
static struct node *parse_local_decl(void);
static struct type *parse_struct_spec(int kind);
static struct type *parse_enum_spec(void);
static void check_function_type(struct type *ty, const char *name);

struct declarator {
	char *name;
	struct type *ty;
};

static struct declarator parse_declarator(struct type *base, bool abstract_ok);
static struct type *parse_type_name(void);

/* ------------------------------------------------------------------ */
/* parser helpers                                                      */
/* ------------------------------------------------------------------ */

static bool is_lvalue(struct node *n)
{
	if (n->kind == ND_VAR)
		return !n->var->is_function && n->ty->kind != TY_ARRAY &&
		       n->ty->kind != TY_FUNC;
	if (n->kind == ND_MEMBER)
		return n->ty->kind != TY_ARRAY && n->ty->kind != TY_FUNC;
	return n->kind == ND_DEREF && n->ty->kind != TY_ARRAY &&
	       n->ty->kind != TY_FUNC;
}

static struct node *new_cast(struct node *e, struct type *ty)
{
	struct node *n = new_node(ND_CAST);

	if (is_aggregate(e->ty) || is_aggregate(ty))
		fatal("cannot cast a struct/union value");
	if (ty->kind == TY_ARRAY || ty->kind == TY_FUNC)
		fatal("cannot cast to an array or function type");
	n->lhs = e;
	n->ty = ty;
	return n;
}

static struct node *cast_node(struct node *e, struct type *ty)
{
	if (e->ty == ty)
		return e;
	return new_cast(e, ty);
}

static struct node *decay_node(struct node *e)
{
	if (e->ty->kind == TY_ARRAY || e->ty->kind == TY_FUNC)
		return new_cast(e, decay(e->ty));
	return e;
}

static struct type *uint_of_size(int size)
{
	return size >= 8 ? ty_ulong : ty_uint;
}

static struct type *int_of_size(int size)
{
	return size >= 8 ? ty_long : ty_int;
}

static struct type *promote_int_type(struct type *t)
{
	if (t->kind == TY_CHAR || t->kind == TY_SHORT)
		return ty_int;
	return t;
}

static struct node *promote_node(struct node *e)
{
	if (is_integer(e->ty))
		return cast_node(e, promote_int_type(e->ty));
	return e;
}

/* Usual arithmetic conversions: only scalar arithmetic types reach here. */
static struct type *usual_arith_type(struct type *a, struct type *b)
{
	if (is_float(a) || is_float(b)) {
		if (a->kind == TY_DOUBLE || b->kind == TY_DOUBLE)
			return ty_double;
		return ty_float;
	}
	a = promote_int_type(a);
	b = promote_int_type(b);
	if (a->size < b->size)
		return b;
	if (b->size < a->size)
		return a;
	if (a->is_unsigned || b->is_unsigned)
		return uint_of_size(a->size);
	return int_of_size(a->size);
}

static struct type *convert_arith(struct node **lhs, struct node **rhs)
{
	struct type *t = usual_arith_type((*lhs)->ty, (*rhs)->ty);

	*lhs = cast_node(*lhs, t);
	*rhs = cast_node(*rhs, t);
	return t;
}

static struct node *new_assign(struct node *lhs, struct node *rhs)
{
	struct node *n;
	struct type *lt;

	if (!is_lvalue(lhs))
		fatal("assignment to non-lvalue");
	rhs = decay_node(rhs);
	lt = lhs->ty;
	n = new_node(ND_ASSIGN);
	if (is_aggregate(lt) || is_aggregate(rhs->ty)) {
		if (!is_aggregate(lt) || !is_aggregate(rhs->ty) ||
		    lt->size != rhs->ty->size)
			fatal("invalid struct/union assignment");
	} else if (lt->kind == TY_PTR) {
		if (is_float(rhs->ty))
			fatal("cannot assign a floating point value to a pointer");
		rhs = cast_node(rhs, lt);
	} else if (is_arith(lt)) {
		if (rhs->ty->kind == TY_PTR)
			fatal("cannot assign a pointer to an arithmetic type");
		if (!is_arith(rhs->ty))
			fatal("invalid assignment operand");
		rhs = cast_node(rhs, lt);
	} else {
		fatal("invalid assignment target");
	}
	n->lhs = lhs;
	n->rhs = rhs;
	n->ty = lt;
	return n;
}

static struct node *to_cond(struct node *e);

static struct node *new_binary(int kind, struct node *lhs, struct node *rhs)
{
	struct node *n = new_node(kind);
	struct type *lt;
	struct type *rt;

	lhs = decay_node(lhs);
	rhs = decay_node(rhs);
	lt = lhs->ty;
	rt = rhs->ty;
	if (is_aggregate(lt) || is_aggregate(rt))
		fatal("invalid operands to binary operator");

	if (kind == ND_LOGAND || kind == ND_LOGOR) {
		n->lhs = to_cond(lhs);
		n->rhs = to_cond(rhs);
		n->ty = ty_int;
		return n;
	}

	if (kind == ND_ADD || kind == ND_SUB) {
		bool lptr = is_ptr_like(lt);
		bool rptr = is_ptr_like(rt);

		if (lptr && rptr) {
			if (kind == ND_ADD)
				fatal("cannot add two pointers");
			n->lhs = lhs;
			n->rhs = rhs;
			n->ty = ty_long;
			return n;
		}
		if (lptr || rptr) {
			struct type *pt = lptr ? lt : rt;
			struct node *idx = lptr ? rhs : lhs;

			if (kind == ND_SUB && !lptr)
				fatal("invalid pointer subtraction");
			if (is_float(idx->ty))
				fatal("invalid floating point operand in pointer arithmetic");
			idx = cast_node(promote_node(idx), ty_long);
			if (lptr) {
				n->lhs = lhs;
				n->rhs = idx;
			} else {
				n->lhs = idx;
				n->rhs = rhs;
			}
			n->ty = pt;
			return n;
		}
	}

	if (kind == ND_SHL || kind == ND_SHR) {
		if (!is_integer(lt) || !is_integer(rt))
			fatal("shift operands must be integers");
		n->lhs = promote_node(lhs);
		n->rhs = promote_node(rhs);
		n->ty = n->lhs->ty;
		return n;
	}

	if (is_ptr_like(lt) || is_ptr_like(rt))
		fatal("invalid pointer arithmetic");
	if (is_float(lt) || is_float(rt)) {
		if (kind != ND_ADD && kind != ND_SUB && kind != ND_MUL &&
		    kind != ND_DIV)
			fatal("operator requires integer operands");
		if (!is_arith(lt) || !is_arith(rt))
			fatal("invalid floating point operands");
	}
	n->ty = convert_arith(&lhs, &rhs);
	n->lhs = lhs;
	n->rhs = rhs;
	return n;
}

static struct node *new_cmp(int kind, struct node *lhs, struct node *rhs)
{
	struct node *n;

	lhs = decay_node(lhs);
	rhs = decay_node(rhs);
	if (is_aggregate(lhs->ty) || is_aggregate(rhs->ty))
		fatal("cannot compare struct/union values");
	if (is_float(lhs->ty) || is_float(rhs->ty)) {
		if (!is_arith(lhs->ty) || !is_arith(rhs->ty))
			fatal("cannot compare a floating point value with a pointer");
		convert_arith(&lhs, &rhs);
	} else if (is_ptr_like(lhs->ty) || is_ptr_like(rhs->ty)) {
		if (is_ptr_like(lhs->ty))
			rhs = cast_node(rhs, lhs->ty);
		else
			lhs = cast_node(lhs, rhs->ty);
	} else {
		convert_arith(&lhs, &rhs);
	}
	n = new_node(kind);
	n->lhs = lhs;
	n->rhs = rhs;
	n->ty = ty_int;
	return n;
}

static struct node *to_cond(struct node *e)
{
	e = decay_node(e);
	if (is_float(e->ty))
		return new_cmp(ND_NE, e, new_float(0.0, e->ty));
	if (!is_scalar(e->ty))
		fatal("scalar value required as a condition");
	return e;
}

static struct node *new_unary(int kind, struct node *e)
{
	struct node *n;
	struct type *t;

	if (kind != ND_ADDR && kind != ND_DEREF)
		e = decay_node(e);
	t = e->ty;
	if (kind != ND_DEREF && kind != ND_ADDR && is_aggregate(t))
		fatal("invalid unary operator on a struct/union value");
	n = new_node(kind);
	switch (kind) {
	case ND_DEREF:
		t = decay(e->ty);
		if (t->kind != TY_PTR)
			fatal("cannot dereference non-pointer");
		n->lhs = e;
		n->ty = t->base;
		break;
	case ND_ADDR:
		if (e->kind == ND_MEMBER && e->bit_width > 0)
			fatal("cannot take the address of a bit-field");
		n->lhs = e;
		n->ty = ty_ptr(e->ty);
		break;
	case ND_NEG:
		if (!is_arith(t))
			fatal("invalid operand to unary minus");
		e = promote_node(e);
		n->lhs = e;
		n->ty = e->ty;
		break;
	case ND_NOT:
		n->lhs = to_cond(e);
		n->ty = ty_int;
		break;
	case ND_BITNOT:
		if (!is_integer(t))
			fatal("operator '~' requires an integer operand");
		e = promote_node(e);
		n->lhs = e;
		n->ty = e->ty;
		break;
	default:
		n->lhs = e;
		n->ty = e->ty;
		break;
	}
	return n;
}

static int temp_id;

static struct obj *new_temp_local(struct type *ty)
{
	char name[32];

	if (!scope)
		fatal("temporary object outside a function scope");
	snprintf(name, sizeof(name), ".tmp%d", temp_id++);
	return new_local(name, ty, false, 0);
}

/* Default argument promotions for unprototyped calls. */
static struct node *default_promote(struct node *e)
{
	e = decay_node(e);
	if (e->ty->kind == TY_FLOAT)
		return new_cast(e, ty_double);
	if (e->ty->kind == TY_CHAR || e->ty->kind == TY_SHORT)
		return new_cast(e, ty_int);
	return e;
}

static struct node *call_arg_cast(struct node *e, struct type *pt)
{
	e = decay_node(e);
	if (is_aggregate(pt)) {
		if (!is_aggregate(e->ty) || e->ty->size != pt->size)
			fatal("incompatible struct/union argument");
		return e;
	}
	if (is_aggregate(e->ty))
		fatal("cannot pass a struct/union value to a scalar parameter");
	if (pt->kind == TY_PTR && is_float(e->ty))
		fatal("cannot pass a floating point value to a pointer parameter");
	if (is_float(pt) && !is_arith(e->ty))
		fatal("cannot pass a non-arithmetic value to a floating point parameter");
	return cast_node(e, pt);
}

/*
 * Apply the current type of the direct callee to a call node.  This runs
 * again when a prototype or definition arrives after the call was parsed,
 * which is how calls to later-defined functions get their final types.
 */
static void apply_call_type_with(struct node *call, struct type *fnty,
				 const char *name, bool can_alloc)
{
	struct type *ret = fnty->base ? fnty->base : ty_int;
	bool unprototyped = fnty->nparams == 0 && !fnty->variadic;
	int i;

	call->ty = ret;
	if (is_aggregate(ret)) {
		if (!ret->is_complete || ret->size <= 0)
			fatal("call to '%s' returns an incomplete struct/union",
			      name);
		if (!call->ret_tmp || call->ret_tmp->ty != ret) {
			if (!can_alloc)
				fatal("call to '%s' changes its result type after a declaration",
				      name);
			call->ret_tmp = new_temp_local(ret);
		}
	} else {
		call->ret_tmp = NULL;
	}
	if (call->nargs > 0 && !call->arg_temps)
		call->arg_temps = xcalloc((size_t)call->nargs,
					  sizeof(*call->arg_temps));
	for (i = 0; i < call->nargs; i++) {
		struct node *a = call->args[i];

		if (i < fnty->nparams)
			a = call_arg_cast(a, fnty->params[i].ty);
		else if (unprototyped || fnty->variadic)
			a = default_promote(a);
		else
			fatal("too many arguments in call to '%s'", name);
		call->args[i] = a;
		if (is_aggregate(a->ty)) {
			if (!a->ty->is_complete || a->ty->size <= 0)
				fatal("incomplete struct/union argument");
			if (!call->arg_temps[i] ||
			    call->arg_temps[i]->ty != a->ty) {
				if (!can_alloc)
					fatal("call to '%s' changes its argument types after a declaration",
					      name);
				call->arg_temps[i] = new_temp_local(a->ty);
			}
		} else {
			call->arg_temps[i] = NULL;
		}
	}
}

static void apply_call_type(struct node *call)
{
	apply_call_type_with(call, call->var->ty, call->var->name, true);
}

/* Compound assignment, desugared for float/double to get C semantics. */
static struct node *new_opassign(struct node *lhs, struct node *rhs, int op)
{
	struct node *n;
	struct type *lt;

	if (!is_lvalue(lhs) || !is_scalar(lhs->ty))
		fatal("invalid compound assignment");
	rhs = decay_node(rhs);
	lt = lhs->ty;
	if (is_float(lt) || is_float(rhs->ty)) {
		struct obj *addr;
		struct node *save;
		struct node *calc;
		struct node *store;
		struct node *result;
		struct node *seq;

		if (op != ND_ADD && op != ND_SUB && op != ND_MUL &&
		    op != ND_DIV)
			fatal("operator requires integer operands");
		if (lhs->kind == ND_MEMBER && lhs->bit_width > 0)
			fatal("floating point compound assignment on a bit-field");
		/*
		 * Evaluate the lvalue once, then compute in the usual
		 * arithmetic type and convert back to the target type.
		 */
		addr = new_temp_local(ty_ptr(lt));
		save = new_assign(new_var_node(addr),
				  new_unary(ND_ADDR, lhs));
		calc = new_binary(op, new_unary(ND_DEREF, new_var_node(addr)),
				  rhs);
		store = new_assign(new_unary(ND_DEREF, new_var_node(addr)),
				   calc);
		result = new_unary(ND_DEREF, new_var_node(addr));
		seq = new_node(ND_COMMA);
		seq->lhs = save;
		seq->rhs = new_node(ND_COMMA);
		seq->rhs->lhs = store;
		seq->rhs->rhs = result;
		seq->ty = result->ty;
		seq->rhs->ty = result->ty;
		return seq;
	}

	n = new_node(ND_OPASSIGN);
	n->lhs = lhs;
	if (is_ptr_like(lt)) {
		if (op != ND_ADD && op != ND_SUB)
			fatal("invalid pointer compound assignment");
		if (!is_integer(rhs->ty))
			fatal("pointer compound assignment requires an integer operand");
		n->rhs = cast_node(promote_node(rhs), ty_long);
	} else if (is_integer(lt)) {
		if (!is_integer(rhs->ty))
			fatal("operator requires integer operands");
		n->rhs = promote_node(rhs);
	} else {
		fatal("invalid compound assignment");
	}
	n->ty = lt;
	n->val = op;
	return n;
}

static void record_pending_call(struct node *call)
{
	struct pending_call *p;
	struct obj *fn = call->var;

	if (!fn || fn->defined)
		return;
	p = xcalloc(1, sizeof(*p));
	p->call = call;
	p->fn = fn;
	p->implicit = fn_state_for(fn)->implicit;
	p->next = pending_calls;
	pending_calls = p;
}

static void patch_calls_for(struct obj *fn)
{
	struct pending_call *p;

	for (p = pending_calls; p; p = p->next) {
		struct type *ret;

		if (p->fn != fn)
			continue;
		ret = fn->ty->base;
		if (p->implicit && (is_float(ret) || is_aggregate(ret)))
			fatal("call to '%s' with a non-integer return type must follow a declaration",
			      fn->name);
		apply_call_type_with(p->call, fn->ty, fn->name, false);
	}
}

/* ------------------------------------------------------------------ */
/* declarations                                                        */
/* ------------------------------------------------------------------ */

static bool declspec_static;
static bool declspec_extern;
static bool declspec_typedef;
static struct type *current_fn_ret;

static struct type *parse_declspec_inner(void)
{
	enum {
		BS_NONE,
		BS_VOID,
		BS_CHAR,
		BS_SHORT,
		BS_INT,
		BS_LONG,
		BS_FLOAT,
		BS_DOUBLE
	} base = BS_NONE;
	bool uns = false;
	bool has_sign = false;
	bool seen = false;
	int line = cur_token()->line;
	int col = cur_token()->col;

	declspec_static = false;
	declspec_extern = false;
	declspec_typedef = false;
	for (;;) {
		if (!seen && is_kind(TK_IDENT)) {
			struct type *td = find_typedef(cur_token()->text);

			if (td) {
				tok_pos++;
				while (consume("const") || consume("volatile") ||
				       consume("restrict"))
					;
				return td;
			}
		}
		if (consume("void")) {
			base = BS_VOID;
			seen = true;
		} else if (consume("char")) {
			base = BS_CHAR;
			seen = true;
		} else if (consume("short")) {
			base = BS_SHORT;
			seen = true;
		} else if (consume("int")) {
			if (base == BS_NONE)
				base = BS_INT;
			seen = true;
		} else if (consume("long")) {
			base = BS_LONG;
			seen = true;
		} else if (consume("unsigned")) {
			uns = true;
			has_sign = true;
			seen = true;
		} else if (consume("signed")) {
			uns = false;
			has_sign = true;
			seen = true;
		} else if (consume("static")) {
			declspec_static = true;
		} else if (consume("extern")) {
			declspec_extern = true;
		} else if (consume("const") || consume("volatile") ||
			   consume("restrict") || consume("register") ||
			   consume("inline")) {
			/* qualifiers are accepted and ignored */
		} else if (consume("typedef")) {
			declspec_typedef = true;
		} else if (is("struct") || is("union") || is("enum")) {
			bool saved_static = declspec_static;
			bool saved_extern = declspec_extern;
			bool saved_typedef = declspec_typedef;
			struct type *ty;

			if (consume("struct")) {
				ty = parse_struct_spec(TY_STRUCT);
			} else if (consume("union")) {
				ty = parse_struct_spec(TY_UNION);
			} else {
				expect("enum");
				ty = parse_enum_spec();
			}
			declspec_static = saved_static;
			declspec_extern = saved_extern;
			declspec_typedef = saved_typedef;
			return ty;
		} else if (consume("__builtin_va_list")) {
			return ty_ptr(ty_void);
		} else if (consume("float")) {
			base = base == BS_LONG ? BS_DOUBLE : BS_FLOAT;
			seen = true;
		} else if (consume("double")) {
			/* "long double" has the same representation as double. */
			base = BS_DOUBLE;
			seen = true;
		} else {
			break;
		}
	}

	if (!seen)
		fatal("%d:%d: expected a type specifier", line, col);

	switch (base) {
	case BS_VOID:
		return ty_void;
	case BS_CHAR:
		/* Plain char is unsigned on RISC-V; "signed char" is explicit. */
		if (!has_sign || uns)
			return ty_char;
		return ty_schar;
	case BS_SHORT:
		return uns ? ty_ushort : ty_short;
	case BS_LONG:
		return uns ? ty_ulong : ty_long;
	case BS_INT:
		return uns ? ty_uint : ty_int;
	case BS_FLOAT:
	case BS_DOUBLE:
		if (has_sign)
			fatal("%d:%d: floating point type cannot be signed or unsigned",
			      line, col);
		return base == BS_FLOAT ? ty_float : ty_double;
	case BS_NONE:
	default:
		return uns ? ty_uint : ty_int;
	}
}

static struct type *parse_declspec(void)
{
	struct type *ty;

	skip_attributes();
	ty = parse_declspec_inner();
	skip_attributes();
	return ty;
}

static struct type *replace_placeholder(struct type *t, struct type *ph,
					struct type *repl)
{
	if (t == ph)
		return repl;
	if (t->kind == TY_PTR) {
		struct type *b = replace_placeholder(t->base, ph, repl);

		return b == t->base ? t : ty_ptr(b);
	}
	if (t->kind == TY_ARRAY) {
		struct type *b = replace_placeholder(t->base, ph, repl);

		return b == t->base ? t : ty_array(b, t->array_len);
	}
	if (t->kind == TY_FUNC) {
		struct type *b = replace_placeholder(t->base, ph, repl);

		return b == t->base ?
			       t :
			       ty_func(b, t->params, t->nparams, t->variadic);
	}
	return t;
}

static struct type *parse_type_suffix(struct type *ty)
{
	if (consume("[")) {
		int len = -1;

		if (!is("]")) {
			len = (int)eval_const(parse_expr());
			if (len < 0)
				fatal("negative array size");
		}
		expect("]");
		ty = parse_type_suffix(ty);
		return ty_array(ty, len);
	}
	if (consume("(")) {
		struct param *params = NULL;
		int nparams = 0;
		int cap = 0;
		bool variadic = false;

		if (consume(")")) {
			/* old-style empty parameter list */
		} else if (is("void") && tokens[tok_pos + 1].text &&
			   !strcmp(tokens[tok_pos + 1].text, ")")) {
			tok_pos += 2;
		} else {
			for (;;) {
				if (consume("...")) {
					variadic = true;
					expect(")");
					break;
				}
				struct type *pt = parse_declspec();
				struct declarator pd =
					parse_declarator(pt, true);

				if (pd.ty->kind == TY_ARRAY)
					pd.ty = ty_ptr(pd.ty->base);
				else if (pd.ty->kind == TY_FUNC)
					pd.ty = ty_ptr(pd.ty);
				if (nparams == cap) {
					cap = cap ? cap * 2 : 8;
					params = xrealloc(
						params,
						(size_t)cap * sizeof(*params));
				}
				params[nparams].name = pd.name;
				params[nparams].ty = pd.ty;
				nparams++;
				if (!consume(",")) {
					expect(")");
					break;
				}
			}
		}
		ty = parse_type_suffix(ty);
		return ty_func(ty, params, nparams, variadic);
	}
	return ty;
}

static struct declarator parse_declarator_inner(struct type *base, bool abstract_ok)
{
	struct declarator d;

	memset(&d, 0, sizeof(d));
	while (consume("*")) {
		base = ty_ptr(base);
		while (consume("const") || consume("volatile") || consume("restrict"))
			;
	}
	if (consume("(")) {
		struct type *ph = new_type(TY_PLACEHOLDER, 0, 0, false);
		struct declarator inner = parse_declarator(ph, abstract_ok);

		expect(")");
		d.name = inner.name;
		d.ty = replace_placeholder(inner.ty, ph,
					   parse_type_suffix(base));
	} else {
		if (is_kind(TK_IDENT) &&
		    (!is_typename() || declspec_typedef)) {
			d.name = xstrdup(cur_token()->text);
			tok_pos++;
		} else if (!abstract_ok) {
			fatal("%d:%d: expected identifier near '%s'",
			      cur_token()->line, cur_token()->col,
			      cur_token()->text);
		}
		d.ty = parse_type_suffix(base);
	}
	skip_attributes();
	if (d.ty->kind == TY_FUNC)
		check_function_type(d.ty, d.name ? d.name : "<function>");
	return d;
}

static struct declarator parse_declarator(struct type *base, bool abstract_ok)
{
	return parse_declarator_inner(base, abstract_ok);
}

static struct type *parse_type_name(void)
{
	struct type *base = parse_declspec();
	struct declarator d = parse_declarator(base, true);

	return d.ty;
}

static void add_member(struct type *t, const char *name, struct type *ty)
{
	int i;

	if (name) {
		for (i = 0; i < t->nmembers; i++) {
			if (t->members[i].name &&
			    !strcmp(t->members[i].name, name))
				fatal("duplicate member '%s'", name);
		}
	}
	t->members = xrealloc(t->members,
			      (size_t)(t->nmembers + 1) * sizeof(*t->members));
	t->members[t->nmembers].name = name ? xstrdup(name) : NULL;
	t->members[t->nmembers].ty = ty;
	t->members[t->nmembers].offset = 0;
	t->members[t->nmembers].bit_offset = 0;
	t->members[t->nmembers].bit_width = 0;
	t->nmembers++;
}

static struct type *parse_struct_spec(int kind)
{
	int tag_kind = kind == TY_STRUCT ? TAG_STRUCT : TAG_UNION;
	char *tag = NULL;
	struct type *ty;

	if (is_kind(TK_IDENT)) {
		tag = xstrdup(cur_token()->text);
		tok_pos++;
	}
	if (consume("{")) {
		int offset = 0;
		int max_align = 1;
		int max_size = 0;
		int bit_unit_offset = -1;
		int bit_unit_bits = 0;
		int bit_cursor = 0;
		bool saw_field = false;
		bool saw_flexible = false;

		if (tag)
			ty = find_tag(tag_kind, tag);
		else
			ty = new_aggregate_type(kind, NULL);
		if (ty->is_complete)
			fatal("redefinition of %s '%s'",
			      kind == TY_STRUCT ? "struct" : "union",
			      tag ? tag : "<anonymous>");
		while (!consume("}")) {
			struct type *base = parse_declspec();
			bool is_td = declspec_typedef;

			if (is_td)
				fatal("typedef inside a struct/union is not supported");
			for (;;) {
				struct declarator d;

				if (saw_flexible)
					fatal("flexible array member must be the last member");
				d = parse_declarator(base, true);
				bool is_bitfield = false;
				long width = 0;
				int idx;

				if (consume(":")) {
					is_bitfield = true;
					if (!is_integer(d.ty))
						fatal("bit-field '%s' must have an integer type",
						      d.name ? d.name :
							       "<anonymous>");
					width = eval_const(parse_cond());
					if (width < 0)
						fatal("negative bit-field width");
					if (width > (long)d.ty->size * 8)
						fatal("bit-field width %ld exceeds its type",
						      width);
					if (width == 0 && d.name)
						fatal("zero-width bit-field must be unnamed");
					if (width == 0 && kind == TY_STRUCT) {
						offset = align_to(offset,
								  d.ty->align);
						bit_unit_offset = -1;
						bit_unit_bits = 0;
						bit_cursor = 0;
					}
					if (width == 0 && d.ty->align > max_align)
						max_align = d.ty->align;
				} else {
					if (!d.name)
						fatal("member name is missing");
				}
				if (d.ty->kind == TY_VOID)
					fatal("void member '%s'",
					      d.name ? d.name : "<anonymous>");
				if (d.ty->kind == TY_FUNC)
					fatal("function member '%s' is not supported",
					      d.name ? d.name : "<anonymous>");
				if (!is_bitfield) {
					bool flexible = d.ty->kind == TY_ARRAY &&
							d.ty->array_len < 0;

					if (flexible && kind == TY_UNION)
						fatal("flexible array member in a union");
					if (!flexible &&
					    ((is_aggregate(d.ty) &&
					      !d.ty->is_complete) ||
					     d.ty->size <= 0))
						fatal("incomplete member type for '%s'",
						      d.name);
					if (flexible)
						saw_flexible = true;
				}
				if (!is_bitfield || width > 0) {
					add_member(ty, d.name, d.ty);
					saw_field = true;
					idx = ty->nmembers - 1;
					if (is_bitfield) {
						ty->members[idx].bit_width =
							(int)width;
						if (kind == TY_UNION) {
							ty->members[idx].offset = 0;
							ty->members[idx].bit_offset = 0;
						} else {
							int unit_bits =
								d.ty->size * 8;

							if (bit_unit_offset < 0 ||
							    bit_unit_bits != unit_bits ||
							    bit_cursor + (int)width >
								    unit_bits) {
								offset = align_to(
									offset,
									d.ty->align);
								bit_unit_offset =
									offset;
								bit_unit_bits =
									unit_bits;
								bit_cursor = 0;
								offset += d.ty->size;
							}
							ty->members[idx].offset =
								bit_unit_offset;
							ty->members[idx]
								.bit_offset =
								bit_cursor;
							bit_cursor += (int)width;
						}
					} else if (kind == TY_STRUCT) {
						offset = align_to(offset,
								  d.ty->align);
						ty->members[idx].offset = offset;
						offset += d.ty->size;
						bit_unit_offset = -1;
						bit_unit_bits = 0;
						bit_cursor = 0;
					} else {
						ty->members[idx].offset = 0;
					}
					if (d.ty->align > max_align)
						max_align = d.ty->align;
					if (d.ty->size > max_size)
						max_size = d.ty->size;
				}
				if (consume(","))
					continue;
				expect(";");
				break;
			}
		}
		if (!saw_field)
			fatal("empty %s is not supported",
			      kind == TY_STRUCT ? "struct" : "union");
		ty->align = max_align;
		ty->size = kind == TY_STRUCT ? align_to(offset, max_align) :
					       align_to(max_size, max_align);
		ty->is_complete = true;
		free(tag);
		return ty;
	}
	if (!tag)
		fatal("expected struct/union tag or definition");
	ty = find_tag(tag_kind, tag);
	free(tag);
	return ty;
}

static struct type *parse_enum_spec(void)
{
	char *tag = NULL;

	if (is_kind(TK_IDENT)) {
		tag = xstrdup(cur_token()->text);
		tok_pos++;
	}
	if (consume("{")) {
		long val = 0;

		while (!consume("}")) {
			char *name;

			if (!is_kind(TK_IDENT))
				fatal("expected enumerator name");
			name = xstrdup(cur_token()->text);
			tok_pos++;
			if (consume("="))
				val = eval_const(parse_assign());
			add_const(name, val);
			free(name);
			val++;
			if (consume(",")) {
				if (is("}")) {
					tok_pos++;
					break;
				}
				continue;
			}
			expect("}");
			break;
		}
		if (tag)
			(void)find_tag(TAG_ENUM, tag);
		free(tag);
		return ty_int;
	}
	if (!tag)
		fatal("expected enum tag or definition");
	(void)find_tag(TAG_ENUM, tag);
	free(tag);
	return ty_int;
}

static void check_function_type(struct type *ty, const char *name)
{
	int i;

	if (ty->kind != TY_FUNC)
		return;
	if (ty->base->kind == TY_ARRAY || ty->base->kind == TY_FUNC)
		fatal("function '%s' cannot return an array or function", name);
	if (is_aggregate(ty->base) && !ty->base->is_complete)
		fatal("function '%s' returns an incomplete struct/union", name);
	for (i = 0; i < ty->nparams; i++) {
		struct type *pt = ty->params[i].ty;

		if (is_aggregate(pt) && !pt->is_complete)
			fatal("function '%s' has an incomplete struct/union parameter",
			      name);
	}
}

static bool types_compatible(struct type *a, struct type *b)
{
	int i;

	if (a == b)
		return true;
	if (a->kind != b->kind)
		return false;
	switch (a->kind) {
	case TY_PTR:
		return types_compatible(a->base, b->base);
	case TY_ARRAY:
		return a->array_len == b->array_len &&
		       types_compatible(a->base, b->base);
	case TY_FUNC:
		if (!types_compatible(a->base, b->base))
			return false;
		if (a->nparams != b->nparams ||
		    a->variadic != b->variadic)
			return false;
		for (i = 0; i < a->nparams; i++) {
			if (!types_compatible(a->params[i].ty,
					      b->params[i].ty))
				return false;
		}
		return true;
	case TY_STRUCT:
	case TY_UNION:
		return false;
	default:
		return a->size == b->size &&
		       a->is_unsigned == b->is_unsigned;
	}
}

static void check_redeclaration(struct obj *fn, struct type *ty)
{
	struct type *old = fn->ty;
	int i;

	if (!old || fn->defined || fn_state_for(fn)->implicit)
		return;
	if (!types_compatible(old->base, ty->base) ||
	    old->base->kind == TY_ARRAY || old->base->kind == TY_FUNC)
		fatal("conflicting types for function '%s'", fn->name);
	/* An old-style declaration does not constrain the parameters. */
	if ((old->nparams == 0 && !old->variadic) ||
	    (ty->nparams == 0 && !ty->variadic))
		return;
	if (old->nparams != ty->nparams || old->variadic != ty->variadic)
		fatal("conflicting types for function '%s'", fn->name);
	for (i = 0; i < old->nparams; i++) {
		if (!types_compatible(old->params[i].ty, ty->params[i].ty))
			fatal("conflicting types for function '%s'",
			      fn->name);
	}
}

static struct obj *get_function_obj(const char *name)
{
	struct obj *o = find_global(name);

	if (o) {
		if (!o->is_function)
			fatal("'%s' is not a function", name);
		return o;
	}
	o = new_global(name, ty_func(ty_int, NULL, 0, true));
	o->is_function = true;
	o->sym = new_sym(name, SEC_TEXT, 0, 4);
	fn_state_for(o)->implicit = true;
	return o;
}

static void declare_function(struct declarator d, bool is_static)
{
	struct obj *fn;

	if (!d.name)
		fatal("function name is missing");
	fn = get_function_obj(d.name);
	check_redeclaration(fn, d.ty);
	if (!fn->defined)
		fn->ty = d.ty;
	fn_state_for(fn)->implicit = false;
	if (!fn->sym)
		fn->sym = new_sym(d.name, SEC_TEXT, 0, 4);
	if (is_static)
		make_local_sym(fn->sym);
	patch_calls_for(fn);
}

static void parse_function(struct declarator d, bool is_static)
{
	struct obj *fn = get_function_obj(d.name);
	int i;

	if (fn->defined)
		fatal("redefinition of function '%s'", d.name);
	check_redeclaration(fn, d.ty);
	fn->ty = d.ty;
	fn->defined = true;
	fn->sym->defined = true;
	fn_state_for(fn)->implicit = false;
	if (is_static)
		make_local_sym(fn->sym);
	patch_calls_for(fn);
	locals_size = 24;
	enter_scope();
	for (i = 0; i < d.ty->nparams; i++) {
		struct param *p = &d.ty->params[i];

		if (!p->name)
			fatal("parameter name is missing in definition of '%s'",
			      d.name);
		p->obj = new_local(p->name, p->ty, true, i);
	}
	if (d.ty->variadic)
		fn->va_area = new_local("__cc_va_area", ty_array(ty_ulong, 16),
					false, 0);
	else
		fn->va_area = NULL;
	if (is_aggregate(d.ty->base))
		fn->ret_ptr = new_local(".retptr", ty_ptr(d.ty->base), false,
					0);
	else
		fn->ret_ptr = NULL;
	current_fn_ret = d.ty->base;
	fn->body = parse_block();
	current_fn_ret = NULL;
	fn->frame_size = align_to(locals_size, 16);
	leave_scope();
}

/* ------------------------------------------------------------------ */
/* constant expressions and global initialisers                        */
/* ------------------------------------------------------------------ */

static long truncate_const(long v, struct type *ty)
{
	if (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_FUNC ||
	    ty->size >= 8)
		return v;
	if (ty->size == 1) {
		v &= 0xff;
		if (!ty->is_unsigned)
			v = (v ^ 0x80L) - 0x80L;
	} else if (ty->size == 2) {
		v &= 0xffff;
		if (!ty->is_unsigned)
			v = (v ^ 0x8000L) - 0x8000L;
	} else if (ty->size == 4) {
		v &= 0xffffffffL;
		if (!ty->is_unsigned)
			v = (v ^ 0x80000000L) - 0x80000000L;
	}
	return v;
}

static long eval_const(struct node *n)
{
	switch (n->kind) {
	case ND_NUM:
		return n->val;
	case ND_NEG:
		return -eval_const(n->lhs);
	case ND_NOT:
		return !eval_const(n->lhs);
	case ND_BITNOT:
		return ~eval_const(n->lhs);
	case ND_CAST:
		return truncate_const(eval_const(n->lhs), n->ty);
	case ND_ADD:
		return eval_const(n->lhs) + eval_const(n->rhs);
	case ND_SUB:
		return eval_const(n->lhs) - eval_const(n->rhs);
	case ND_MUL:
		return eval_const(n->lhs) * eval_const(n->rhs);
	case ND_DIV: {
		long b = eval_const(n->rhs);

		if (b == 0)
			fatal("division by zero in constant expression");
		return eval_const(n->lhs) / b;
	}
	case ND_MOD: {
		long b = eval_const(n->rhs);

		if (b == 0)
			fatal("division by zero in constant expression");
		return eval_const(n->lhs) % b;
	}
	case ND_BITAND:
		return eval_const(n->lhs) & eval_const(n->rhs);
	case ND_BITOR:
		return eval_const(n->lhs) | eval_const(n->rhs);
	case ND_BITXOR:
		return eval_const(n->lhs) ^ eval_const(n->rhs);
	case ND_SHL:
		return eval_const(n->lhs) << eval_const(n->rhs);
	case ND_SHR:
		return eval_const(n->lhs) >> eval_const(n->rhs);
	case ND_EQ:
		return eval_const(n->lhs) == eval_const(n->rhs);
	case ND_NE:
		return eval_const(n->lhs) != eval_const(n->rhs);
	case ND_LT:
		return eval_const(n->lhs) < eval_const(n->rhs);
	case ND_LE:
		return eval_const(n->lhs) <= eval_const(n->rhs);
	case ND_GT:
		return eval_const(n->lhs) > eval_const(n->rhs);
	case ND_GE:
		return eval_const(n->lhs) >= eval_const(n->rhs);
	case ND_LOGAND:
		return eval_const(n->lhs) && eval_const(n->rhs);
	case ND_LOGOR:
		return eval_const(n->lhs) || eval_const(n->rhs);
	case ND_COND:
		return eval_const(n->cond) ? eval_const(n->then) :
					     eval_const(n->els);
	default:
		fatal("not a constant expression");
		return 0;
	}
}

static bool expr_has_float(struct node *n)
{
	if (!n)
		return false;
	if (is_float(n->ty))
		return true;
	switch (n->kind) {
	case ND_FLOAT:
		return true;
	case ND_CAST:
	case ND_NEG:
	case ND_NOT:
	case ND_BITNOT:
		return expr_has_float(n->lhs);
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
	case ND_LOGAND:
	case ND_LOGOR:
	case ND_COMMA:
		return expr_has_float(n->lhs) || expr_has_float(n->rhs);
	case ND_COND:
		return expr_has_float(n->cond) || expr_has_float(n->then) ||
		       expr_has_float(n->els);
	default:
		return false;
	}
}

static double round_const_type(struct type *ty, double v)
{
	if (ty->kind == TY_FLOAT)
		return (double)(float)v;
	return v;
}

/*
 * Constant evaluator for initialisers that involve floating point.
 * Integer-only subtrees are evaluated by eval_const so 64-bit values keep
 * their exact bit pattern.
 */
static double eval_const_double(struct node *n)
{
	double a;
	double b;

	if (!expr_has_float(n))
		return (double)eval_const(n);
	switch (n->kind) {
	case ND_FLOAT:
		return round_const_type(n->ty, n->fval);
	case ND_CAST:
		a = eval_const_double(n->lhs);
		if (is_float(n->ty))
			return round_const_type(n->ty, a);
		if (n->ty->kind == TY_VOID)
			return 0.0;
		if (n->ty->kind == TY_PTR)
			return (double)(long)a;
		return (double)truncate_const((long)a, n->ty);
	case ND_NEG:
		return round_const_type(n->ty, -eval_const_double(n->lhs));
	case ND_NOT:
		return eval_const_double(n->lhs) == 0.0;
	case ND_BITNOT:
		return (double)~((long)eval_const_double(n->lhs));
	case ND_ADD:
		return round_const_type(n->ty, eval_const_double(n->lhs) +
							eval_const_double(n->rhs));
	case ND_SUB:
		return round_const_type(n->ty, eval_const_double(n->lhs) -
							eval_const_double(n->rhs));
	case ND_MUL:
		return round_const_type(n->ty, eval_const_double(n->lhs) *
							eval_const_double(n->rhs));
	case ND_DIV:
		a = eval_const_double(n->lhs);
		b = eval_const_double(n->rhs);
		if (is_float(n->ty)) {
			if (b == 0.0)
				fatal("division by zero in constant expression");
			return round_const_type(n->ty, a / b);
		}
		if ((long)b == 0)
			fatal("division by zero in constant expression");
		return (double)((long)a / (long)b);
	case ND_MOD:
		a = eval_const_double(n->lhs);
		b = eval_const_double(n->rhs);
		if ((long)b == 0)
			fatal("division by zero in constant expression");
		return (double)((long)a % (long)b);
	case ND_BITAND:
		return (double)((long)eval_const_double(n->lhs) &
				(long)eval_const_double(n->rhs));
	case ND_BITOR:
		return (double)((long)eval_const_double(n->lhs) |
				(long)eval_const_double(n->rhs));
	case ND_BITXOR:
		return (double)((long)eval_const_double(n->lhs) ^
				(long)eval_const_double(n->rhs));
	case ND_SHL:
		return (double)((long)eval_const_double(n->lhs) <<
				(long)eval_const_double(n->rhs));
	case ND_SHR:
		return (double)((long)eval_const_double(n->lhs) >>
				(long)eval_const_double(n->rhs));
	case ND_EQ:
		return eval_const_double(n->lhs) == eval_const_double(n->rhs);
	case ND_NE:
		return eval_const_double(n->lhs) != eval_const_double(n->rhs);
	case ND_LT:
		return eval_const_double(n->lhs) < eval_const_double(n->rhs);
	case ND_LE:
		return eval_const_double(n->lhs) <= eval_const_double(n->rhs);
	case ND_GT:
		return eval_const_double(n->lhs) > eval_const_double(n->rhs);
	case ND_GE:
		return eval_const_double(n->lhs) >= eval_const_double(n->rhs);
	case ND_LOGAND:
		return eval_const_double(n->lhs) != 0.0 &&
		       eval_const_double(n->rhs) != 0.0;
	case ND_LOGOR:
		return eval_const_double(n->lhs) != 0.0 ||
		       eval_const_double(n->rhs) != 0.0;
	case ND_COND:
		return eval_const_double(n->cond) != 0.0 ?
			       eval_const_double(n->then) :
			       eval_const_double(n->els);
	case ND_COMMA:
		(void)eval_const_double(n->lhs);
		return eval_const_double(n->rhs);
	default:
		fatal("not a constant expression");
		return 0.0;
	}
}

static void store_int(unsigned char *p, long val, int size)
{
	int i;

	for (i = 0; i < size; i++)
		p[i] = (unsigned char)((unsigned long)val >> (i * 8));
}

static void store_float(unsigned char *p, double val, struct type *ty)
{
	if (ty->kind == TY_FLOAT) {
		float f = (float)val;

		memcpy(p, &f, sizeof(f));
	} else {
		double d = val;

		memcpy(p, &d, sizeof(d));
	}
}

static void materialize_scalar_global(struct sym *s, struct type *ty,
				      struct node *expr, int offset)
{
	if (is_float(ty)) {
		store_float(s->init + offset, eval_const_double(expr), ty);
		return;
	}
	if (expr_has_float(expr)) {
		long v = (long)eval_const_double(expr);

		store_int(s->init + offset, truncate_const(v, ty), ty->size);
		return;
	}
	store_int(s->init + offset, eval_const(expr), ty->size);
}

static struct sym *new_string_sym(const unsigned char *bytes, int len)
{
	static int str_id;
	char name[32];
	struct sym *s;

	snprintf(name, sizeof(name), ".Lstr%d", str_id++);
	s = new_sym(name, SEC_DATA, len, 1);
	s->init = xmalloc((size_t)(len > 0 ? len : 1));
	memcpy(s->init, bytes, (size_t)len);
	s->defined = true;
	s->is_static =
		true; /* local data; never resolved by name across units */
	return s;
}

/* Concatenate adjacent string literal tokens, as C does. */
static int concat_strings(unsigned char **out)
{
	struct buf b;

	buf_init(&b);
	while (is_kind(TK_STR)) {
		struct token *t = cur_token();
		int n = t->str_len;

		if (b.len > 0)
			b.len--; /* drop the previous terminating NUL */
		buf_grow(&b, n);
		memcpy(b.data + b.len, t->str, (size_t)n);
		b.len += n;
		tok_pos++;
	}
	*out = b.data;
	return b.len;
}

enum {
	INIT_EXPR = 0,
	INIT_LIST,
	INIT_STRING,
};

struct init {
	int kind;
	struct node *expr;
	struct init **items;
	int *indices;
	int n;
	int max_index;
	unsigned char *str;
	int str_len;
};

static struct init *parse_initializer(struct type *ty);
static void add_data_reloc(struct sym *s, int offset, struct sym *target,
			   long addend);
static void materialize_global_init(struct sym *s, struct type *ty,
				    struct init *in, int offset);

static void parse_global_var(struct declarator d, bool is_static,
			     bool is_extern)
{
	struct obj *v;
	struct sym *s;
	struct init *init = NULL;
	bool has_init;

	if (!d.name)
		fatal("variable name is missing");
	if (d.ty->kind == TY_FUNC)
		fatal("unexpected function declarator");
	if (d.ty->kind == TY_VOID)
		fatal("incomplete type for '%s'", d.name);
	if (is_aggregate(d.ty) && !d.ty->is_complete)
		fatal("incomplete type for '%s'", d.name);
	v = find_global(d.name);
	if (v && v->is_function)
		fatal("'%s' is already a function", d.name);
	has_init = is("=");
	if (v && v->defined) {
		if (is_extern && !has_init)
			return;
		fatal("redefinition of '%s'", d.name);
	}
	if (has_init) {
		tok_pos++;
		init = parse_initializer(d.ty);
	}
	if (d.ty->kind == TY_ARRAY && d.ty->array_len < 0) {
		int len;

		if (is_extern && !has_init) {
			/* An incomplete extern array is completed by a definition. */
		} else {
			if (!init)
				fatal("array size is missing for '%s'", d.name);
			if (init->kind == INIT_STRING)
				len = init->str_len;
			else if (init->kind == INIT_LIST)
				len = init->max_index;
			else
				fatal("cannot infer array size for '%s'", d.name);
			if (len <= 0)
				fatal("zero-length array '%s'", d.name);
			d.ty->array_len = len;
			d.ty->size = d.ty->base->size * len;
		}
	}
	if (init && init->kind == INIT_STRING) {
		if (d.ty->kind != TY_ARRAY || d.ty->base->kind != TY_CHAR)
			fatal("invalid string initialiser");
		if (init->str_len - 1 > d.ty->array_len)
			fatal("initialiser string too long");
	}
	if (!v)
		v = new_global(d.name, d.ty);
	else
		v->ty = d.ty;
	v->is_function = false;
	s = v->sym;
	if (!s) {
		s = new_sym(d.name, SEC_BSS, d.ty->size, d.ty->align);
		v->sym = s;
	}
	if (is_static)
		make_local_sym(s);
	s->size = d.ty->size;
	s->align = d.ty->align;
	if (has_init) {
		v->defined = true;
		s->defined = true;
		s->sec = SEC_DATA;
		s->size = d.ty->size;
		s->init = xcalloc(1, (size_t)(s->size > 0 ? s->size : 1));
		materialize_global_init(s, d.ty, init, 0);
	} else if (is_extern) {
		v->defined = false;
		s->defined = false;
		s->sec = SEC_BSS;
	} else {
		if (d.ty->kind == TY_ARRAY && d.ty->array_len < 0)
			fatal("array size is missing for '%s'", d.name);
		v->defined = true;
		s->defined = true;
		s->sec = SEC_BSS;
		s->size = d.ty->size;
	}
	if (d.ty->size <= 0 &&
	    !(is_extern && !has_init && d.ty->kind == TY_ARRAY))
		fatal("incomplete type for '%s'", d.name);
}

/* ---- local aggregate initialisers ---- */

static struct init *new_init(int kind)
{
	struct init *in = xcalloc(1, sizeof(*in));

	in->kind = kind;
	return in;
}

static void init_add(struct init *in, struct init *item, int index)
{
	in->items =
		xrealloc(in->items, (size_t)(in->n + 1) * sizeof(*in->items));
	in->indices = xrealloc(in->indices,
			       (size_t)(in->n + 1) * sizeof(*in->indices));
	in->items[in->n] = item;
	in->indices[in->n] = index;
	in->n++;
	if (index + 1 > in->max_index)
		in->max_index = index + 1;
}

static int member_index(struct type *ty, const char *name)
{
	int i;

	for (i = 0; i < ty->nmembers; i++) {
		if (ty->members[i].name &&
		    !strcmp(ty->members[i].name, name))
			return i;
	}
	fatal("no member named '%s'", name);
	return -1;
}

static int next_named_member(struct type *ty, int idx)
{
	while (idx < ty->nmembers && !ty->members[idx].name)
		idx++;
	return idx;
}

static struct init *parse_flat(struct type *ty);

static struct init *parse_initializer(struct type *ty)
{
	struct init *in;

	if (ty->kind == TY_ARRAY && ty->base->kind == TY_CHAR &&
	    is_kind(TK_STR)) {
		in = new_init(INIT_STRING);
		in->str_len = concat_strings(&in->str);
		return in;
	}

	if (is("{")) {
		/* Scalar braces, e.g. int x = { 1 }; */
		if (!is_aggregate(ty) && ty->kind != TY_ARRAY) {
			struct init *one;

			tok_pos++;
			one = parse_initializer(ty);
			consume(",");
			expect("}");
			return one;
		}
		tok_pos++;
		in = new_init(INIT_LIST);
		if (consume("}"))
			return in;
		{
			int next_index = 0;

			for (;;) {
				struct type *elem = NULL;
				int idx = -1;

				if (consume(".")) {
					char *mname;

					if (ty->kind != TY_STRUCT &&
					    ty->kind != TY_UNION)
						fatal("field designator on a non-struct type");
					if (!is_kind(TK_IDENT))
						fatal("expected field name after '.'");
					mname = xstrdup(cur_token()->text);
					tok_pos++;
					idx = member_index(ty, mname);
					free(mname);
					expect("=");
				} else if (consume("[")) {
					long v;

					if (ty->kind != TY_ARRAY)
						fatal("array designator on a non-array type");
					v = eval_const(parse_expr());
					expect("]");
					expect("=");
					if (v < 0)
						fatal("negative array designator");
					idx = (int)v;
				} else {
					idx = next_index;
					if (ty->kind == TY_STRUCT ||
					    ty->kind == TY_UNION)
						idx = next_named_member(ty,
									idx);
				}

				if (ty->kind == TY_ARRAY) {
					if (idx < 0 || (ty->array_len >= 0 &&
							idx >= ty->array_len))
						fatal("array initialiser index out of range");
					elem = ty->base;
				} else if (ty->kind == TY_STRUCT) {
					if (idx < 0 || idx >= ty->nmembers)
						fatal("struct initialiser index out of range");
					elem = ty->members[idx].ty;
				} else if (ty->kind == TY_UNION) {
					if (idx < 0 || idx >= ty->nmembers)
						fatal("union initialiser index out of range");
					if (in->n >= 1)
						fatal("too many union initialisers");
					elem = ty->members[idx].ty;
				} else {
					fatal("invalid braced initialiser");
					return NULL;
				}

				if (is_aggregate(elem) ||
				    elem->kind == TY_ARRAY) {
					if (is("{"))
						init_add(
							in,
							parse_initializer(elem),
							idx);
					else
						init_add(in, parse_flat(elem),
							 idx);
				} else {
					init_add(in, parse_initializer(elem),
						 idx);
				}

				next_index = idx + 1;
				if (consume(",")) {
					if (is("}")) {
						tok_pos++;
						break;
					}
					continue;
				}
				expect("}");
				break;
			}
		}
		return in;
	}

	in = new_init(INIT_EXPR);
	in->expr = parse_assign();
	return in;
}

static struct init *parse_flat(struct type *ty)
{
	struct init *in;
	int n;
	int i;

	if (ty->kind == TY_ARRAY && ty->base->kind == TY_CHAR &&
	    is_kind(TK_STR))
		return parse_initializer(ty);
	if (is("{"))
		return parse_initializer(ty);
	if (!is_aggregate(ty) && ty->kind != TY_ARRAY)
		return parse_initializer(ty);

	in = new_init(INIT_LIST);
	if (ty->kind == TY_ARRAY) {
		n = ty->array_len;
		if (n < 0)
			fatal("cannot flatten an incomplete array");
		for (i = 0; i < n; i++) {
			init_add(in, parse_flat(ty->base), i);
			if (i + 1 < n) {
				if (is("}"))
					break;
				expect(",");
				if (is("}"))
					break;
			}
		}
		return in;
	}
	if (ty->kind == TY_UNION) {
		for (i = 0; i < ty->nmembers; i++) {
			if (ty->members[i].name)
				break;
		}
		if (i < ty->nmembers)
			init_add(in, parse_flat(ty->members[i].ty), i);
		return in;
	}
	if (ty->kind != TY_STRUCT) {
		fatal("invalid flat initialiser");
		return NULL;
	}
	{
		int added = 0;

		for (i = 0; i < ty->nmembers; i++) {
			if (!ty->members[i].name)
				continue;
			if (added > 0) {
				if (is("}"))
					break;
				expect(",");
				if (is("}"))
					break;
			}
			init_add(in, parse_flat(ty->members[i].ty), i);
			added++;
		}
	}
	return in;
}

static struct node *new_member_node(struct node *base, struct member *m)
{
	struct node *n = new_node(ND_MEMBER);

	n->lhs = base;
	n->member_offset = m->offset;
	n->bit_offset = m->bit_offset;
	n->bit_width = m->bit_width;
	n->ty = m->ty;
	return n;
}

static struct node *new_element_node(struct node *base, int idx)
{
	return new_unary(ND_DEREF, new_binary(ND_ADD, base, new_num(idx)));
}

static void append_stmt(struct node ***tail, struct node *n)
{
	**tail = n;
	*tail = &n->next;
}

static void append_expr_stmt(struct node ***tail, struct node *expr)
{
	struct node *s = new_node(ND_EXPR_STMT);

	s->lhs = expr;
	append_stmt(tail, s);
}

static void build_local_init(struct node *lv, struct type *ty, struct init *in,
			     struct node ***tail)
{
	int i;

	if (!in)
		return;
	if (in->kind == INIT_EXPR) {
		append_expr_stmt(tail, new_assign(lv, in->expr));
		return;
	}
	if (in->kind == INIT_STRING) {
		int copy;

		if (ty->kind != TY_ARRAY || ty->base->kind != TY_CHAR)
			fatal("invalid string initialiser");
		copy = in->str_len < ty->array_len ? in->str_len :
						     ty->array_len;
		for (i = 0; i < copy; i++) {
			struct node *el = new_element_node(lv, i);

			append_expr_stmt(tail,
					 new_assign(el, new_num(in->str[i])));
		}
		return;
	}
	if (in->kind == INIT_LIST) {
		if (ty->kind == TY_ARRAY) {
			for (i = 0; i < in->n; i++) {
				int idx = in->indices[i];
				struct node *el;

				if (idx < 0 || (ty->array_len >= 0 &&
						idx >= ty->array_len))
					fatal("array initialiser index out of range");
				el = new_element_node(lv, idx);
				build_local_init(el, ty->base, in->items[i],
						 tail);
			}
			return;
		}
		if (ty->kind == TY_STRUCT) {
			for (i = 0; i < in->n; i++) {
				int idx = in->indices[i];
				struct node *mem;

				if (idx < 0 || idx >= ty->nmembers)
					fatal("struct initialiser index out of range");
				mem = new_member_node(lv, &ty->members[idx]);
				build_local_init(mem, ty->members[idx].ty,
						 in->items[i], tail);
			}
			return;
		}
		if (ty->kind == TY_UNION) {
			if (in->n > 1)
				fatal("too many union initialisers");
			if (in->n == 1) {
				int idx = in->indices[0];
				struct node *mem;

				if (idx < 0 || idx >= ty->nmembers)
					fatal("union initialiser index out of range");
				mem = new_member_node(lv, &ty->members[idx]);
				build_local_init(mem, ty->members[idx].ty,
						 in->items[0], tail);
			}
			return;
		}
		fatal("braced initialiser for a scalar");
	}
}

static void add_data_reloc(struct sym *s, int offset, struct sym *target,
			   long addend)
{
	struct data_reloc *dr = xcalloc(1, sizeof(*dr));

	dr->offset = offset;
	dr->size = 8;
	dr->kind = DR_ABS;
	dr->sym = target;
	dr->addend = addend;
	dr->next = s->drel;
	s->drel = dr;
}

static bool global_pointer_value(struct node *e, struct sym **target,
				 long *addend);

static bool global_addr_of(struct node *e, struct sym **target, long *addend)
{
	if (e->kind == ND_VAR && !e->var->is_local) {
		*target = e->var->sym;
		*addend = 0;
		return true;
	}
	if (e->kind == ND_MEMBER) {
		if (!global_addr_of(e->lhs, target, addend))
			return false;
		*addend += e->member_offset;
		return true;
	}
	if (e->kind == ND_DEREF)
		return global_pointer_value(e->lhs, target, addend);
	if (e->kind == ND_CAST)
		return global_addr_of(e->lhs, target, addend);
	if (e->kind == ND_STR) {
		*target = e->sym;
		*addend = 0;
		return true;
	}
	return false;
}

static long global_addr_step(struct type *t)
{
	t = decay(t);
	if (t->kind != TY_PTR || !t->base || t->base->size <= 0)
		return 1;
	return t->base->size;
}

static bool global_pointer_value(struct node *e, struct sym **target,
				 long *addend)
{
	if (e->kind == ND_STR) {
		*target = e->sym;
		*addend = 0;
		return true;
	}
	if (e->kind == ND_CAST)
		return global_pointer_value(e->lhs, target, addend);
	if (e->kind == ND_VAR && !e->var->is_local &&
	    (e->var->is_function || e->ty->kind == TY_ARRAY)) {
		*target = e->var->sym;
		*addend = 0;
		return true;
	}
	if (e->kind == ND_MEMBER && !e->var &&
	    (e->ty->kind == TY_ARRAY || e->ty->kind == TY_FUNC)) {
		/* An array/function member decays to the address of the member. */
		return global_addr_of(e, target, addend);
	}
	if (e->kind == ND_ADDR)
		return global_addr_of(e->lhs, target, addend);
	if (e->kind == ND_ADD || e->kind == ND_SUB) {
		struct node *p;
		struct node *idx;
		long step;
		long n;

		if (global_pointer_value(e->lhs, target, addend)) {
			p = e->lhs;
			idx = e->rhs;
		} else if (e->kind == ND_ADD &&
			   global_pointer_value(e->rhs, target, addend)) {
			p = e->rhs;
			idx = e->lhs;
		} else {
			return false;
		}
		step = global_addr_step(p->ty);
		n = eval_const(idx);
		if (e->kind == ND_SUB)
			n = -n;
		*addend += n * step;
		return true;
	}
	return false;
}

static void materialize_global_bitfield(struct sym *s, struct member *m,
					struct init *in, int offset)
{
	unsigned long unit = 0;
	unsigned long fmask;
	unsigned long mask;
	long v;
	int i;
	int bytes = m->ty->size;

	if (!in || in->kind != INIT_EXPR)
		fatal("unsupported bit-field initialiser");
	if (expr_has_float(in->expr))
		v = (long)eval_const_double(in->expr);
	else
		v = eval_const(in->expr);
	if (m->bit_width >= 64)
		fmask = ~0UL;
	else
		fmask = (1UL << m->bit_width) - 1;
	mask = fmask << m->bit_offset;
	for (i = 0; i < bytes; i++)
		unit |= (unsigned long)s->init[offset + i] << (i * 8);
	unit = (unit & ~mask) | (((unsigned long)v << m->bit_offset) & mask);
	store_int(s->init + offset, (long)unit, bytes);
}

static void materialize_global_init(struct sym *s, struct type *ty,
				    struct init *in, int offset)
{
	int i;

	if (!in)
		return;
	if (in->kind == INIT_EXPR) {
		if (is_aggregate(ty))
			fatal("aggregate copy initialiser is not a constant expression");
		if (ty->kind == TY_PTR) {
			struct sym *target;
			long addend;

			if (global_pointer_value(in->expr, &target, &addend)) {
				add_data_reloc(s, offset, target, addend);
				return;
			}
		}
		materialize_scalar_global(s, ty, in->expr, offset);
		return;
	}
	if (in->kind == INIT_STRING) {
		int copy;

		if (ty->kind != TY_ARRAY || ty->base->kind != TY_CHAR)
			fatal("invalid string initialiser");
		copy = in->str_len < ty->array_len ? in->str_len :
						     ty->array_len;
		memcpy(s->init + offset, in->str, (size_t)copy);
		return;
	}
	if (in->kind == INIT_LIST) {
		if (ty->kind == TY_ARRAY) {
			for (i = 0; i < in->n; i++) {
				int idx = in->indices[i];

				if (idx < 0 || (ty->array_len >= 0 &&
						idx >= ty->array_len))
					fatal("array initialiser index out of range");
				materialize_global_init(
					s, ty->base, in->items[i],
					offset + idx * ty->base->size);
			}
			return;
		}
		if (ty->kind == TY_STRUCT) {
			for (i = 0; i < in->n; i++) {
				int idx = in->indices[i];
				struct member *m;

				if (idx < 0 || idx >= ty->nmembers)
					fatal("struct initialiser index out of range");
				m = &ty->members[idx];
				if (m->bit_width > 0)
					materialize_global_bitfield(
						s, m, in->items[i],
						offset + m->offset);
				else
					materialize_global_init(
						s, m->ty, in->items[i],
						offset + m->offset);
			}
			return;
		}
		if (ty->kind == TY_UNION) {
			if (in->n > 1)
				fatal("too many union initialisers");
			if (in->n == 1) {
				int idx = in->indices[0];
				struct member *m;

				if (idx < 0 || idx >= ty->nmembers)
					fatal("union initialiser index out of range");
				m = &ty->members[idx];
				if (m->bit_width > 0)
					materialize_global_bitfield(
						s, m, in->items[0],
						offset + m->offset);
				else
					materialize_global_init(
						s, m->ty, in->items[0],
						offset + m->offset);
			}
			return;
		}
		fatal("braced initialiser for a scalar");
	}
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static int local_sym_id;

static char *new_local_sym_name(const char *name)
{
	char *p;
	size_t n = strlen(name) + 40;

	p = xmalloc(n);
	snprintf(p, n, "%s.__s%d_%d", name, unit_id, local_sym_id++);
	return p;
}

static struct obj *new_scoped_obj(const char *name, struct type *ty,
				  struct sym *s)
{
	struct obj *o = xcalloc(1, sizeof(*o));

	if (!scope)
		fatal("object declared outside a block scope");
	o->name = xstrdup(name);
	o->ty = ty;
	o->is_local = false;
	o->defined = s->defined;
	o->sym = s;
	o->next = scope->vars;
	scope->vars = o;
	return o;
}

static void infer_local_array_size(struct type *ty, struct init *in,
				   const char *name)
{
	int len;

	if (ty->kind != TY_ARRAY || ty->array_len >= 0)
		return;
	if (!in)
		fatal("array '%s' needs a size", name);
	if (in->kind == INIT_STRING)
		len = in->str_len;
	else if (in->kind == INIT_LIST)
		len = in->max_index;
	else
		fatal("cannot infer size of array '%s'", name);
	if (len <= 0)
		fatal("zero-length array '%s'", name);
	ty->array_len = len;
	ty->size = ty->base->size * len;
}

static struct node *parse_local_decl(void)
{
	struct type *base = parse_declspec();
	bool is_typedef = declspec_typedef;
	bool is_static = declspec_static;
	bool is_extern = declspec_extern;
	struct node *head = NULL;
	struct node **tail = &head;

	if (consume(";")) {
		if (is_typedef)
			fatal("typedef requires a name");
		return NULL;
	}
	if (is_typedef) {
		for (;;) {
			struct declarator d = parse_declarator(base, false);

			if (!d.name)
				fatal("typedef name is missing");
			add_typedef(d.name, d.ty);
			if (consume(","))
				continue;
			expect(";");
			break;
		}
		return NULL;
	}

	for (;;) {
		struct declarator d = parse_declarator(base, false);
		struct init *in = NULL;
		struct obj *v;
		struct node *lv;

		if (!d.name)
			fatal("variable name is missing");
		if (d.ty->kind == TY_FUNC) {
			if (is_static)
				fatal("block-scope static function declaration '%s'",
				      d.name);
			declare_function(d, false);
			if (consume("="))
				fatal("function '%s' cannot have an initialiser",
				      d.name);
			if (consume(","))
				continue;
			expect(";");
			break;
		}
		if (is_static && is_extern)
			fatal("'%s' cannot be both static and extern", d.name);
		if (consume("=")) {
			if (is_extern)
				fatal("'extern' local '%s' cannot have an initialiser",
				      d.name);
			in = parse_initializer(d.ty);
		}
		if (!is_extern)
			infer_local_array_size(d.ty, in, d.name);
		if (in && in->kind == INIT_STRING) {
			if (d.ty->kind != TY_ARRAY ||
			    d.ty->base->kind != TY_CHAR)
				fatal("invalid string initialiser");
			if (in->str_len - 1 > d.ty->array_len)
				fatal("initialiser string too long");
		}
		if (is_extern) {
			struct sym *s;
			struct obj *g = find_global(d.name);

			if (d.ty->kind == TY_VOID ||
			    (is_aggregate(d.ty) && !d.ty->is_complete) ||
			    (d.ty->kind != TY_ARRAY && d.ty->size <= 0))
				fatal("incomplete type for extern '%s'", d.name);
			if (g && g->sym) {
				if (g->is_function)
					fatal("extern '%s' is a function",
					      d.name);
				if (g->ty->size != d.ty->size)
					fatal("conflicting types for extern '%s'",
					      d.name);
				s = g->sym;
			} else {
				s = new_sym(d.name, SEC_BSS, d.ty->size,
					    d.ty->align);
				s->defined = false;
			}
			new_scoped_obj(d.name, d.ty, s);
		} else if (is_static) {
			struct sym *s;
			char *sname;
			int sec = in ? SEC_DATA : SEC_BSS;

			if (d.ty->kind == TY_VOID ||
			    (is_aggregate(d.ty) && !d.ty->is_complete) ||
			    d.ty->size <= 0)
				fatal("incomplete type for static '%s'", d.name);
			sname = new_local_sym_name(d.name);
			s = new_sym(sname, sec, d.ty->size, d.ty->align);
			free(sname);
			s->defined = true;
			s->is_static = true;
			if (in) {
				s->init = xcalloc(1, (size_t)s->size);
				materialize_global_init(s, d.ty, in, 0);
			}
			new_scoped_obj(d.name, d.ty, s);
		} else {
			v = new_local(d.name, d.ty, false, 0);
			if (in) {
				lv = new_var_node(v);
				if ((is_aggregate(d.ty) ||
				     d.ty->kind == TY_ARRAY) &&
				    in->kind != INIT_EXPR) {
					struct node *mz =
						new_node(ND_MEMZERO);

					mz->lhs = lv;
					mz->val = d.ty->size;
					append_stmt(&tail, mz);
					build_local_init(lv, d.ty, in, &tail);
				} else {
					if (!in->expr)
						fatal("invalid initialiser");
					append_expr_stmt(&tail,
							 new_assign(lv,
								    in->expr));
				}
			}
		}
		if (consume(","))
			continue;
		expect(";");
		break;
	}
	return head;
}

static struct node *wrap_block(struct node *body)
{
	struct node *n = new_node(ND_BLOCK);

	n->body = body;
	return n;
}

static struct node *parse_stmt(void)
{
	if (is("{"))
		return parse_block();
	if (consume("switch")) {
		struct node *n = new_node(ND_SWITCH);
		struct switch_ctx ctx;

		expect("(");
		n->cond = parse_expr();
		if (!is_integer(decay(n->cond->ty)))
			fatal("switch condition must have an integer type");
		expect(")");
		ctx.cases = NULL;
		ctx.prev = switches;
		switches = &ctx;
		n->body = parse_stmt();
		switches = ctx.prev;
		n->cases = ctx.cases;
		return n;
	}
	if (consume("case")) {
		struct node *n;
		struct case_entry *e;
		long val;

		if (!switches)
			fatal("case outside switch");
		val = eval_const(parse_expr());
		expect(":");
		for (e = switches->cases; e; e = e->next) {
			if (!e->is_default && e->val == val)
				fatal("duplicate case value");
		}
		n = new_node(ND_CASE);
		n->val = val;
		n->case_label = new_label();
		e = xcalloc(1, sizeof(*e));
		e->node = n;
		e->val = val;
		e->next = switches->cases;
		switches->cases = e;
		n->body = parse_stmt();
		return n;
	}
	if (consume("default")) {
		struct node *n;
		struct case_entry *e;

		if (!switches)
			fatal("default outside switch");
		expect(":");
		for (e = switches->cases; e; e = e->next) {
			if (e->is_default)
				fatal("duplicate default label");
		}
		n = new_node(ND_DEFAULT);
		n->case_label = new_label();
		e = xcalloc(1, sizeof(*e));
		e->node = n;
		e->is_default = true;
		e->next = switches->cases;
		switches->cases = e;
		n->body = parse_stmt();
		return n;
	}
	if (consume("goto")) {
		struct node *n = new_node(ND_GOTO);

		if (!is_kind(TK_IDENT))
			fatal("expected label name after goto");
		n->label_name = xstrdup(cur_token()->text);
		tok_pos++;
		expect(";");
		return n;
	}
	if (is_kind(TK_IDENT) && tokens[tok_pos + 1].text &&
	    !strcmp(tokens[tok_pos + 1].text, ":")) {
		struct node *n = new_node(ND_LABEL);

		n->label_name = xstrdup(cur_token()->text);
		tok_pos += 2;
		n->body = parse_stmt();
		return n;
	}
	if (consume("if")) {
		struct node *n = new_node(ND_IF);

		expect("(");
		n->cond = to_cond(parse_expr());
		expect(")");
		n->then = parse_stmt();
		if (consume("else"))
			n->els = parse_stmt();
		return n;
	}
	if (consume("while")) {
		struct node *n = new_node(ND_WHILE);

		expect("(");
		n->cond = to_cond(parse_expr());
		expect(")");
		n->body = parse_stmt();
		return n;
	}
	if (consume("do")) {
		struct node *n = new_node(ND_DO);

		n->body = parse_stmt();
		expect("while");
		expect("(");
		n->cond = to_cond(parse_expr());
		expect(")");
		expect(";");
		return n;
	}
	if (consume("for")) {
		struct node *n = new_node(ND_FOR);

		expect("(");
		enter_scope();
		if (consume(";")) {
			n->init = NULL;
		} else if (is_typename()) {
			n->init = wrap_block(parse_local_decl());
		} else {
			struct node *e = parse_expr();

			expect(";");
			n->init = new_node(ND_EXPR_STMT);
			n->init->lhs = e;
		}
		if (!consume(";")) {
			n->cond = to_cond(parse_expr());
			expect(";");
		}
		if (!is(")"))
			n->inc = parse_expr();
		expect(")");
		n->body = parse_stmt();
		leave_scope();
		return n;
	}
	if (consume("return")) {
		struct node *n = new_node(ND_RETURN);

		if (!consume(";")) {
			struct node *e = parse_expr();

			if (current_fn_ret) {
				struct type *rt = current_fn_ret;

				if (is_aggregate(rt)) {
					if (!is_aggregate(e->ty) ||
					    e->ty->size != rt->size)
						fatal("return type mismatch in function returning a struct/union");
				} else {
					e = decay_node(e);
					if (rt->kind == TY_VOID) {
						e = new_cast(e, ty_void);
					} else if (is_float(rt)) {
						if (!is_arith(e->ty))
							fatal("cannot return a non-arithmetic value from a function returning %s",
							      rt->kind == TY_FLOAT ?
								      "float" :
								      "double");
						e = cast_node(e, rt);
					} else if (rt->kind == TY_PTR) {
						if (is_float(e->ty))
							fatal("cannot return a floating point value from a function returning a pointer");
						e = cast_node(e, rt);
					} else {
						if (!is_arith(e->ty))
							fatal("cannot return a non-arithmetic value from a function returning an integer");
						e = cast_node(e, rt);
					}
				}
			}
			n->lhs = e;
			expect(";");
		}
		return n;
	}
	if (consume("break")) {
		expect(";");
		return new_node(ND_BREAK);
	}
	if (consume("continue")) {
		expect(";");
		return new_node(ND_CONTINUE);
	}
	if (consume(";"))
		return new_node(ND_NULL);
	if (is_typename()) {
		return wrap_block(parse_local_decl());
	}
	{
		struct node *n = new_node(ND_EXPR_STMT);

		n->lhs = parse_expr();
		expect(";");
		return n;
	}
}

static struct node *parse_block(void)
{
	struct node *head = NULL;
	struct node **tail = &head;
	struct node *block;

	expect("{");
	enter_scope();
	while (!consume("}")) {
		struct node *s;

		if (is_kind(TK_EOF))
			fatal("unexpected end of file inside block");
		s = parse_stmt();
		*tail = s;
		tail = &s->next;
	}
	leave_scope();
	block = new_node(ND_BLOCK);
	block->body = head;
	return block;
}

/* ------------------------------------------------------------------ */
/* expressions                                                         */
/* ------------------------------------------------------------------ */

static struct node *parse_primary(void)
{
	if (is_kind(TK_NUM)) {
		struct node *n = new_num(cur_token()->val);

		tok_pos++;
		return n;
	}
	if (is_kind(TK_FLOAT)) {
		struct token *t = cur_token();
		struct node *n = new_float(
			t->fval, t->is_float32 ? ty_float : ty_double);

		tok_pos++;
		return n;
	}
	if (is_kind(TK_STR)) {
		unsigned char *bytes;
		int len = concat_strings(&bytes);
		struct sym *s = new_string_sym(bytes, len);
		struct node *n = new_node(ND_STR);

		free(bytes);
		n->sym = s;
		n->ty = ty_array(ty_char, len);
		return n;
	}
	if (is_kind(TK_IDENT)) {
		char *name = xstrdup(cur_token()->text);
		struct obj *v = find_local(name);
		long cval;

		if (!v)
			v = find_global(name);
		if (v) {
			free(name);
			tok_pos++;
			return new_var_node(v);
		}
		if (find_const(name, &cval)) {
			free(name);
			tok_pos++;
			return new_num(cval);
		}
		if (tokens[tok_pos + 1].text &&
		    !strcmp(tokens[tok_pos + 1].text, "(")) {
			v = get_function_obj(name);
			free(name);
			tok_pos++;
			return new_var_node(v);
		}
		fatal("%d:%d: undefined variable '%s'", cur_token()->line,
		      cur_token()->col, name);
	}
	if (consume("(")) {
		struct node *n = parse_expr();

		expect(")");
		return n;
	}
	fatal("%d:%d: expected expression near '%s'", cur_token()->line,
	      cur_token()->col, cur_token()->text);
	return NULL;
}

static long sizeof_type(struct type *ty)
{
	if (is_aggregate(ty) && !ty->is_complete)
		fatal("sizeof on an incomplete struct/union");
	if (ty->kind == TY_ARRAY && ty->array_len < 0)
		fatal("sizeof on an incomplete array");
	if (ty->size <= 0)
		fatal("sizeof on an incomplete type");
	return ty->size;
}

static struct member *find_member(struct type *ty, const char *name)
{
	int i;

	if (!is_aggregate(ty))
		fatal("member access on a non-struct/union type");
	if (!ty->is_complete)
		fatal("member access on an incomplete type");
	for (i = 0; i < ty->nmembers; i++) {
		if (ty->members[i].name &&
		    !strcmp(ty->members[i].name, name))
			return &ty->members[i];
	}
	fatal("no member named '%s' in %s '%s'", name,
	      ty->kind == TY_STRUCT ? "struct" : "union",
	      ty->tag ? ty->tag : "<anonymous>");
	return NULL;
}

static struct node *parse_postfix(void)
{
	struct node *n = parse_primary();

	for (;;) {
		if (consume("[")) {
			struct node *idx = parse_expr();

			expect("]");
			n = new_unary(ND_DEREF, new_binary(ND_ADD, n, idx));
			continue;
		}
		if (is(".") || is("->")) {
			bool arrow = is("->");
			char *mname;
			struct member *m;
			struct node *mem;

			tok_pos++;
			if (!is_kind(TK_IDENT))
				fatal("expected member name");
			mname = xstrdup(cur_token()->text);
			tok_pos++;
			if (arrow)
				n = new_unary(ND_DEREF, n);
			m = find_member(n->ty, mname);
			mem = new_node(ND_MEMBER);
			mem->lhs = n;
			mem->member_offset = m->offset;
			mem->bit_offset = m->bit_offset;
			mem->bit_width = m->bit_width;
			mem->ty = m->ty;
			free(mname);
			n = mem;
			continue;
		}
		if (consume("(")) {
			if (n->kind == ND_VAR && n->var->name &&
			    !strcmp(n->var->name, "__builtin_va_arg")) {
				struct node *ap = parse_assign();
				struct node *va;
				struct type *ty;

				expect(",");
				ty = parse_type_name();
				expect(")");
				va = new_node(ND_VA_ARG);
				va->lhs = ap;
				va->ty = ty;
				n = va;
				continue;
			}
			struct node *call;
			struct node *callee = NULL;
			struct obj *fn = NULL;
			struct type *fnty;
			struct type *ctype = n->ty;
			struct node **args = NULL;
			int nargs = 0;
			int cap = 0;

			if (n->kind == ND_VAR && n->var->is_function) {
				fn = n->var;
				fnty = fn->ty;
			} else if (ctype->kind == TY_FUNC) {
				fnty = ctype;
				callee = n;
			} else if (ctype->kind == TY_PTR &&
				   ctype->base->kind == TY_FUNC) {
				fnty = ctype->base;
				callee = n;
			} else {
				fatal("called object is not a function");
				return NULL;
			}
			if (!consume(")")) {
				for (;;) {
					if (nargs == cap) {
						cap = cap ? cap * 2 : 8;
						args = xrealloc(
							args,
							(size_t)cap *
								sizeof(*args));
					}
					args[nargs++] = parse_assign();
					if (!consume(",")) {
						expect(")");
						break;
					}
				}
			}
			call = new_node(ND_CALL);
			call->var = fn;
			call->callee = callee;
			call->args = args;
			call->nargs = nargs;
			if (fn) {
				apply_call_type(call);
				record_pending_call(call);
			} else {
				apply_call_type_with(call, fnty,
						     "<function pointer>", true);
			}
			n = call;
			continue;
		}
		if (consume("++")) {
			struct node *u = new_node(ND_POSTINC);

			if (!is_lvalue(n) || !is_scalar(n->ty))
				fatal("invalid increment operand");
			u->lhs = n;
			u->ty = n->ty;
			u->val = 1;
			n = u;
			continue;
		}
		if (consume("--")) {
			struct node *u = new_node(ND_POSTINC);

			if (!is_lvalue(n) || !is_scalar(n->ty))
				fatal("invalid decrement operand");
			u->lhs = n;
			u->ty = n->ty;
			u->val = -1;
			n = u;
			continue;
		}
		break;
	}
	return n;
}

static struct node *parse_unary(void)
{
	if (consume("+")) {
		struct node *e = parse_unary();

		if (!is_arith(e->ty))
			fatal("invalid operand to unary plus");
		return promote_node(e);
	}
	if (consume("-"))
		return new_unary(ND_NEG, parse_unary());
	if (consume("!"))
		return new_unary(ND_NOT, parse_unary());
	if (consume("~"))
		return new_unary(ND_BITNOT, parse_unary());
	if (consume("&"))
		return new_unary(ND_ADDR, parse_unary());
	if (consume("*"))
		return new_unary(ND_DEREF, parse_unary());
	if (consume("++")) {
		struct node *e = parse_unary();
		struct node *u = new_node(ND_PREINC);

		if (!is_lvalue(e) || !is_scalar(e->ty))
			fatal("invalid increment operand");
		u->lhs = e;
		u->ty = e->ty;
		u->val = 1;
		return u;
	}
	if (consume("--")) {
		struct node *e = parse_unary();
		struct node *u = new_node(ND_PREINC);

		if (!is_lvalue(e) || !is_scalar(e->ty))
			fatal("invalid decrement operand");
		u->lhs = e;
		u->ty = e->ty;
		u->val = -1;
		return u;
	}
	if (consume("sizeof")) {
		if (is("(") && is_typename_at(tok_pos + 1)) {
			struct type *ty;

			tok_pos++;
			ty = parse_type_name();
			expect(")");
			return new_num(sizeof_type(ty));
		}
		{
			struct node *e = parse_unary();

			if (e->kind == ND_MEMBER && e->bit_width > 0)
				fatal("sizeof cannot be applied to a bit-field");
			return new_num(sizeof_type(e->ty));
		}
	}
	if (is("(") && is_typename_at(tok_pos + 1)) {
		struct type *ty;

		tok_pos++;
		ty = parse_type_name();
		expect(")");
		return new_cast(parse_unary(), ty);
	}
	return parse_postfix();
}

static struct node *parse_mul(void)
{
	struct node *n = parse_unary();

	for (;;) {
		if (consume("*"))
			n = new_binary(ND_MUL, n, parse_unary());
		else if (consume("/"))
			n = new_binary(ND_DIV, n, parse_unary());
		else if (consume("%"))
			n = new_binary(ND_MOD, n, parse_unary());
		else
			return n;
	}
}

static struct node *parse_add(void)
{
	struct node *n = parse_mul();

	for (;;) {
		if (consume("+"))
			n = new_binary(ND_ADD, n, parse_mul());
		else if (consume("-"))
			n = new_binary(ND_SUB, n, parse_mul());
		else
			return n;
	}
}

static struct node *parse_shift(void)
{
	struct node *n = parse_add();

	for (;;) {
		if (consume("<<"))
			n = new_binary(ND_SHL, n, parse_add());
		else if (consume(">>"))
			n = new_binary(ND_SHR, n, parse_add());
		else
			return n;
	}
}

static struct node *parse_relational(void)
{
	struct node *n = parse_shift();

	for (;;) {
		if (consume("<"))
			n = new_cmp(ND_LT, n, parse_shift());
		else if (consume("<="))
			n = new_cmp(ND_LE, n, parse_shift());
		else if (consume(">"))
			n = new_cmp(ND_GT, n, parse_shift());
		else if (consume(">="))
			n = new_cmp(ND_GE, n, parse_shift());
		else
			return n;
	}
}

static struct node *parse_equality(void)
{
	struct node *n = parse_relational();

	for (;;) {
		if (consume("=="))
			n = new_cmp(ND_EQ, n, parse_relational());
		else if (consume("!="))
			n = new_cmp(ND_NE, n, parse_relational());
		else
			return n;
	}
}

static struct node *parse_bitand(void)
{
	struct node *n = parse_equality();

	while (consume("&"))
		n = new_binary(ND_BITAND, n, parse_equality());
	return n;
}

static struct node *parse_bitxor(void)
{
	struct node *n = parse_bitand();

	while (consume("^"))
		n = new_binary(ND_BITXOR, n, parse_bitand());
	return n;
}

static struct node *parse_bitor(void)
{
	struct node *n = parse_bitxor();

	while (consume("|"))
		n = new_binary(ND_BITOR, n, parse_bitxor());
	return n;
}

static struct node *parse_logand(void)
{
	struct node *n = parse_bitor();

	while (consume("&&"))
		n = new_binary(ND_LOGAND, n, parse_bitor());
	return n;
}

static struct node *parse_logor(void)
{
	struct node *n = parse_logand();

	while (consume("||"))
		n = new_binary(ND_LOGOR, n, parse_logand());
	return n;
}

static struct node *parse_cond(void)
{
	struct node *n = parse_logor();

	if (consume("?")) {
		struct node *c = new_node(ND_COND);
		struct node *then;
		struct node *els;

		c->cond = to_cond(n);
		then = decay_node(parse_expr());
		expect(":");
		els = decay_node(parse_cond());
		if (is_float(then->ty) || is_float(els->ty)) {
			if (!is_arith(then->ty) || !is_arith(els->ty))
				fatal("invalid conditional operands");
			c->ty = convert_arith(&then, &els);
		} else if (is_ptr_like(then->ty) ||
			   is_ptr_like(els->ty)) {
			if (is_ptr_like(then->ty) && is_ptr_like(els->ty)) {
				els = cast_node(els, then->ty);
				c->ty = then->ty;
			} else if (is_ptr_like(then->ty)) {
				els = cast_node(els, then->ty);
				c->ty = then->ty;
			} else {
				then = cast_node(then, els->ty);
				c->ty = els->ty;
			}
		} else if (is_aggregate(then->ty) ||
			   is_aggregate(els->ty)) {
			if (!is_aggregate(then->ty) ||
			    !is_aggregate(els->ty) ||
			    then->ty->size != els->ty->size)
				fatal("invalid conditional operands");
			c->ty = then->ty;
		} else if (then->ty->kind == TY_VOID &&
			   els->ty->kind == TY_VOID) {
			c->ty = ty_void;
		} else if (is_arith(then->ty) && is_arith(els->ty)) {
			c->ty = convert_arith(&then, &els);
		} else {
			fatal("invalid conditional operands");
		}
		c->then = then;
		c->els = els;
		return c;
	}
	return n;
}

static struct node *parse_assign(void)
{
	struct node *lhs = parse_cond();
	static const struct {
		const char *op;
		int kind;
	} ops[] = {
		{ "+=", ND_ADD },   { "-=", ND_SUB },	 { "*=", ND_MUL },
		{ "/=", ND_DIV },   { "%=", ND_MOD },	 { "&=", ND_BITAND },
		{ "|=", ND_BITOR }, { "^=", ND_BITXOR }, { "<<=", ND_SHL },
		{ ">>=", ND_SHR },
	};

	if (consume("="))
		return new_assign(lhs, parse_assign());
	for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
		if (consume(ops[i].op))
			return new_opassign(lhs, parse_assign(),
					    ops[i].kind);
	}
	return lhs;
}

static struct node *parse_expr(void)
{
	struct node *n = parse_assign();

	while (consume(",")) {
		struct node *c = new_node(ND_COMMA);

		c->lhs = n;
		c->rhs = parse_assign();
		c->ty = c->rhs->ty;
		n = c;
	}
	return n;
}

void parse_translation_unit(void)
{
	while (!is_kind(TK_EOF)) {
		struct type *base;
		bool is_typedef;
		bool is_static;
		bool is_extern;

		if (consume(";"))
			continue;
		base = parse_declspec();
		is_typedef = declspec_typedef;
		is_static = declspec_static;
		is_extern = declspec_extern;
		if (consume(";")) {
			if (is_typedef)
				fatal("typedef requires a name");
			continue;
		}
		if (is_typedef) {
			for (;;) {
				struct declarator d =
					parse_declarator(base, false);

				if (!d.name)
					fatal("typedef name is missing");
				add_typedef(d.name, d.ty);
				if (consume(","))
					continue;
				expect(";");
				break;
			}
			continue;
		}
		for (;;) {
			struct declarator d = parse_declarator(base, false);

			if (d.ty->kind == TY_FUNC) {
				if (is("{")) {
					parse_function(d, is_static);
					break;
				}
				declare_function(d, is_static);
				if (consume(","))
					continue;
				expect(";");
				break;
			}
			parse_global_var(d, is_static, is_extern);
			if (consume(","))
				continue;
			expect(";");
			break;
		}
	}
}

void reset_parser_state(void)
{
	typedefs = NULL;
	tags = NULL;
	consts = NULL;
	fn_states = NULL;
	pending_calls = NULL;
	current_fn_ret = NULL;
	tok_pos = 0;
	temp_id = 0;
	local_sym_id = 0;
}
