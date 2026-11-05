#include "cc.h"

/* ------------------------------------------------------------------ */
/* parser state                                                        */
/* ------------------------------------------------------------------ */

static int tok_pos;

struct switch_ctx {
	struct case_entry *cases;
	struct switch_ctx *prev;
};

static struct switch_ctx *switches;

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
		"void",	  "char",   "short",	"int",	  "long",    "unsigned",
		"signed", "const",  "volatile", "static", "extern",  "register",
		"inline", "struct", "union",	"enum",	  "typedef",
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
	n->lhs = e;
	n->ty = ty;
	return n;
}

static struct node *new_assign(struct node *lhs, struct node *rhs)
{
	struct node *n = new_node(ND_ASSIGN);

	if (!is_lvalue(lhs))
		fatal("assignment to non-lvalue");
	if (is_aggregate(lhs->ty) || is_aggregate(rhs->ty)) {
		if (!is_aggregate(lhs->ty) || !is_aggregate(rhs->ty) ||
		    lhs->ty->size != rhs->ty->size)
			fatal("invalid struct/union assignment");
	}
	n->lhs = lhs;
	n->rhs = rhs;
	n->ty = lhs->ty;
	return n;
}

static struct node *new_binary(int kind, struct node *lhs, struct node *rhs)
{
	struct node *n = new_node(kind);
	struct type *lt = decay(lhs->ty);
	struct type *rt = decay(rhs->ty);

	if (is_aggregate(lt) || is_aggregate(rt))
		fatal("invalid operands to binary operator");
	n->lhs = lhs;
	n->rhs = rhs;

	if (kind == ND_ADD) {
		if (is_ptr_like(lt)) {
			n->ty = lt;
		} else if (is_ptr_like(rt)) {
			n->ty = rt;
		} else if (lt->size == 8 || rt->size == 8) {
			n->ty = (lt->is_unsigned || rt->is_unsigned) ?
					ty_ulong :
					ty_long;
		} else {
			n->ty = (lt->is_unsigned || rt->is_unsigned) ? ty_uint :
								       ty_int;
		}
	} else if (kind == ND_SUB) {
		if (is_ptr_like(lt) && is_ptr_like(rt))
			n->ty = ty_long;
		else if (is_ptr_like(lt))
			n->ty = lt;
		else if (is_ptr_like(rt))
			fatal("invalid pointer subtraction");
		else if (lt->size == 8 || rt->size == 8)
			n->ty = (lt->is_unsigned || rt->is_unsigned) ?
					ty_ulong :
					ty_long;
		else
			n->ty = (lt->is_unsigned || rt->is_unsigned) ? ty_uint :
								       ty_int;
	} else if (kind == ND_SHL || kind == ND_SHR) {
		n->ty = lt;
	} else {
		if (is_ptr_like(lt) || is_ptr_like(rt))
			fatal("invalid pointer arithmetic");
		if (lt->size == 8 || rt->size == 8)
			n->ty = (lt->is_unsigned || rt->is_unsigned) ?
					ty_ulong :
					ty_long;
		else
			n->ty = (lt->is_unsigned || rt->is_unsigned) ? ty_uint :
								       ty_int;
	}
	return n;
}

static struct node *new_cmp(int kind, struct node *lhs, struct node *rhs)
{
	struct node *n = new_node(kind);

	if (is_aggregate(decay(lhs->ty)) || is_aggregate(decay(rhs->ty)))
		fatal("cannot compare struct/union values");
	n->lhs = lhs;
	n->rhs = rhs;
	n->ty = ty_int;
	return n;
}

static struct node *new_unary(int kind, struct node *e)
{
	struct node *n = new_node(kind);

	if ((kind == ND_NEG || kind == ND_NOT || kind == ND_BITNOT) &&
	    is_aggregate(e->ty))
		fatal("invalid unary operator on a struct/union value");
	n->lhs = e;
	switch (kind) {
	case ND_DEREF: {
		struct type *t = decay(e->ty);

		if (t->kind != TY_PTR)
			fatal("cannot dereference non-pointer");
		n->ty = t->base;
		break;
	}
	case ND_ADDR:
		n->ty = ty_ptr(e->ty);
		break;
	case ND_NOT:
		n->ty = ty_int;
		break;
	default:
		n->ty = e->ty;
		break;
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* declarations                                                        */
/* ------------------------------------------------------------------ */

static bool declspec_static;
static bool declspec_extern;
static bool declspec_typedef;

static struct type *parse_declspec(void)
{
	enum {
		BS_NONE,
		BS_VOID,
		BS_CHAR,
		BS_SHORT,
		BS_INT,
		BS_LONG
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
				while (consume("const") || consume("volatile"))
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
			   consume("register") || consume("inline")) {
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
		} else if (consume("float") || consume("double")) {
			fatal("%d:%d: floating point is not supported",
			      cur_token()->line, cur_token()->col);
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
	case BS_NONE:
	default:
		return uns ? ty_uint : ty_int;
	}
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

static struct declarator parse_declarator(struct type *base, bool abstract_ok)
{
	struct declarator d;

	memset(&d, 0, sizeof(d));
	while (consume("*")) {
		base = ty_ptr(base);
		consume("const");
		consume("volatile");
	}
	if (consume("(")) {
		struct type *ph = new_type(TY_PLACEHOLDER, 0, 0, false);
		struct declarator inner = parse_declarator(ph, abstract_ok);

		expect(")");
		d.name = inner.name;
		d.ty = replace_placeholder(inner.ty, ph,
					   parse_type_suffix(base));
	} else {
		if (is_kind(TK_IDENT) && !is_typename()) {
			d.name = xstrdup(cur_token()->text);
			tok_pos++;
		} else if (!abstract_ok) {
			fatal("%d:%d: expected identifier near '%s'",
			      cur_token()->line, cur_token()->col,
			      cur_token()->text);
		}
		d.ty = parse_type_suffix(base);
	}
	if (d.ty->kind == TY_FUNC)
		check_function_type(d.ty, d.name ? d.name : "<function>");
	return d;
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

	for (i = 0; i < t->nmembers; i++) {
		if (!strcmp(t->members[i].name, name))
			fatal("duplicate member '%s'", name);
	}
	t->members = xrealloc(t->members,
			      (size_t)(t->nmembers + 1) * sizeof(*t->members));
	t->members[t->nmembers].name = xstrdup(name);
	t->members[t->nmembers].ty = ty;
	t->members[t->nmembers].offset = 0;
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
				struct declarator d =
					parse_declarator(base, false);
				int idx;

				if (!d.name)
					fatal("member name is missing");
				if (d.ty->kind == TY_VOID)
					fatal("void member '%s'", d.name);
				if (d.ty->kind == TY_FUNC)
					fatal("function member '%s' is not supported",
					      d.name);
				if (consume(":"))
					fatal("bit-fields are not supported");
				if (d.ty->kind == TY_ARRAY &&
				    d.ty->array_len < 0)
					fatal("flexible array member '%s' is not supported",
					      d.name);
				if ((is_aggregate(d.ty) &&
				     !d.ty->is_complete) ||
				    d.ty->size <= 0)
					fatal("incomplete member type for '%s'",
					      d.name);
				add_member(ty, d.name, d.ty);
				idx = ty->nmembers - 1;
				if (kind == TY_STRUCT) {
					offset = align_to(offset, d.ty->align);
					ty->members[idx].offset = offset;
					offset += d.ty->size;
				} else {
					ty->members[idx].offset = 0;
				}
				if (d.ty->align > max_align)
					max_align = d.ty->align;
				if (d.ty->size > max_size)
					max_size = d.ty->size;
				if (consume(","))
					continue;
				expect(";");
				break;
			}
		}
		if (ty->nmembers == 0)
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
	if (is_aggregate(ty->base))
		fatal("function '%s' returns a struct/union by value (not supported)",
		      name);
	for (i = 0; i < ty->nparams; i++) {
		if (is_aggregate(ty->params[i].ty))
			fatal("function '%s' has a struct/union parameter by value (not supported)",
			      name);
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
	return o;
}

static void declare_function(struct declarator d, bool is_static)
{
	struct obj *fn;

	if (!d.name)
		fatal("function name is missing");
	fn = get_function_obj(d.name);
	if (!fn->defined)
		fn->ty = d.ty;
	if (!fn->sym)
		fn->sym = new_sym(d.name, SEC_TEXT, 0, 4);
	if (is_static)
		make_local_sym(fn->sym);
}

static void parse_function(struct declarator d, bool is_static)
{
	struct obj *fn = get_function_obj(d.name);
	int i;

	if (fn->defined)
		fatal("redefinition of function '%s'", d.name);
	fn->ty = d.ty;
	fn->defined = true;
	fn->sym->defined = true;
	if (is_static)
		make_local_sym(fn->sym);
	locals_size = 16;
	enter_scope();
	for (i = 0; i < d.ty->nparams; i++) {
		struct param *p = &d.ty->params[i];

		if (!p->name)
			fatal("parameter name is missing in definition of '%s'",
			      d.name);
		if (i >= 8)
			fatal("more than 8 parameters are not supported");
		p->obj = new_local(p->name, p->ty, true, i);
	}
	if (d.ty->variadic)
		fn->va_area = new_local("__cc_va_area", ty_array(ty_ulong, 8),
					false, 0);
	else
		fn->va_area = NULL;
	fn->body = parse_block();
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

static void store_int(unsigned char *p, long val, int size)
{
	int i;

	for (i = 0; i < size; i++)
		p[i] = (unsigned char)((unsigned long)val >> (i * 8));
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
		if (!strcmp(ty->members[i].name, name))
			return i;
	}
	fatal("no member named '%s'", name);
	return -1;
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
	} else if (ty->kind == TY_STRUCT) {
		n = ty->nmembers;
	} else if (ty->kind == TY_UNION) {
		n = ty->nmembers > 0 ? 1 : 0;
	} else {
		fatal("invalid flat initialiser");
		return NULL;
	}
	for (i = 0; i < n; i++) {
		struct type *sub;

		if (ty->kind == TY_ARRAY)
			sub = ty->base;
		else if (ty->kind == TY_STRUCT)
			sub = ty->members[i].ty;
		else
			sub = ty->members[0].ty;
		init_add(in, parse_flat(sub), i);
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

static struct node *new_member_node(struct node *base, struct member *m)
{
	struct node *n = new_node(ND_MEMBER);

	n->lhs = base;
	n->member_offset = m->offset;
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
	dr->sym = target;
	dr->addend = addend;
	dr->next = s->drel;
	s->drel = dr;
}

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
	return false;
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
	if (e->kind == ND_ADDR)
		return global_addr_of(e->lhs, target, addend);
	return false;
}

static void materialize_global_init(struct sym *s, struct type *ty,
				    struct init *in, int offset)
{
	int i;

	if (!in)
		return;
	if (in->kind == INIT_EXPR) {
		if (is_aggregate(ty))
			fatal("global aggregate copy initialiser is not supported");
		if (ty->kind == TY_PTR) {
			struct sym *target;
			long addend;

			if (global_pointer_value(in->expr, &target, &addend)) {
				add_data_reloc(s, offset, target, addend);
				return;
			}
		}
		{
			long val = eval_const(in->expr);

			store_int(s->init + offset, val, ty->size);
		}
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

				if (idx < 0 || idx >= ty->nmembers)
					fatal("struct initialiser index out of range");
				materialize_global_init(
					s, ty->members[idx].ty, in->items[i],
					offset + ty->members[idx].offset);
			}
			return;
		}
		if (ty->kind == TY_UNION) {
			if (in->n > 1)
				fatal("too many union initialisers");
			if (in->n == 1) {
				int idx = in->indices[0];

				if (idx < 0 || idx >= ty->nmembers)
					fatal("union initialiser index out of range");
				materialize_global_init(
					s, ty->members[idx].ty, in->items[0],
					offset + ty->members[idx].offset);
			}
			return;
		}
		fatal("braced initialiser for a scalar");
	}
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static struct node *parse_local_decl(void)
{
	struct type *base = parse_declspec();
	bool is_typedef = declspec_typedef;
	struct node *head = NULL;
	struct node **tail = &head;

	if (declspec_static || declspec_extern)
		fatal("%d:%d: static/extern locals are not supported",
		      cur_token()->line, cur_token()->col);
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
		struct obj *v;
		struct init *in = NULL;
		struct node *lv;

		if (!d.name)
			fatal("variable name is missing");
		if (d.ty->kind == TY_FUNC)
			fatal("local function declarations are not supported");
		if (consume("="))
			in = parse_initializer(d.ty);
		if (d.ty->kind == TY_ARRAY && d.ty->array_len < 0) {
			int len;

			if (!in)
				fatal("local array '%s' needs a size", d.name);
			if (in->kind == INIT_STRING)
				len = in->str_len;
			else if (in->kind == INIT_LIST)
				len = in->max_index;
			else
				fatal("cannot infer size of local array '%s'",
				      d.name);
			if (len <= 0)
				fatal("zero-length local array '%s'", d.name);
			d.ty->array_len = len;
			d.ty->size = d.ty->base->size * len;
		}
		if (in && in->kind == INIT_STRING) {
			if (d.ty->kind != TY_ARRAY ||
			    d.ty->base->kind != TY_CHAR)
				fatal("invalid string initialiser");
			if (in->str_len - 1 > d.ty->array_len)
				fatal("initialiser string too long");
		}
		v = new_local(d.name, d.ty, false, 0);
		if (in) {
			lv = new_var_node(v);
			if ((is_aggregate(d.ty) || d.ty->kind == TY_ARRAY) &&
			    in->kind != INIT_EXPR) {
				struct node *mz = new_node(ND_MEMZERO);

				mz->lhs = lv;
				mz->val = d.ty->size;
				append_stmt(&tail, mz);
				build_local_init(lv, d.ty, in, &tail);
			} else {
				if (!in->expr)
					fatal("invalid initialiser");
				append_expr_stmt(&tail,
						 new_assign(lv, in->expr));
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
		n->cond = parse_expr();
		expect(")");
		n->then = parse_stmt();
		if (consume("else"))
			n->els = parse_stmt();
		return n;
	}
	if (consume("while")) {
		struct node *n = new_node(ND_WHILE);

		expect("(");
		n->cond = parse_expr();
		expect(")");
		n->body = parse_stmt();
		return n;
	}
	if (consume("do")) {
		struct node *n = new_node(ND_DO);

		n->body = parse_stmt();
		expect("while");
		expect("(");
		n->cond = parse_expr();
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
			n->cond = parse_expr();
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
			n->lhs = parse_expr();
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
		if (!strcmp(ty->members[i].name, name))
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
			mem->ty = m->ty;
			free(mname);
			n = mem;
			continue;
		}
		if (consume("(")) {
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
			if (nargs > 8)
				fatal("more than 8 call arguments are not supported");
			for (int ai = 0; ai < nargs; ai++) {
				if (is_aggregate(args[ai]->ty))
					fatal("struct/union arguments are not supported");
			}
			call = new_node(ND_CALL);
			call->var = fn;
			call->callee = callee;
			call->args = args;
			call->nargs = nargs;
			call->ty = fnty->base ? fnty->base : ty_int;
			if (is_aggregate(call->ty))
				fatal("functions returning a struct/union by value are not supported");
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
	if (consume("+"))
		return parse_unary();
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

		c->cond = n;
		c->then = parse_expr();
		expect(":");
		c->els = parse_cond();
		c->ty = c->then->ty->size >= c->els->ty->size ? c->then->ty :
								c->els->ty;
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
		if (consume(ops[i].op)) {
			struct node *n = new_node(ND_OPASSIGN);

			if (!is_lvalue(lhs) || !is_scalar(lhs->ty))
				fatal("invalid compound assignment");
			n->lhs = lhs;
			n->rhs = parse_assign();
			n->ty = lhs->ty;
			n->val = ops[i].kind;
			return n;
		}
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
	tok_pos = 0;
}
