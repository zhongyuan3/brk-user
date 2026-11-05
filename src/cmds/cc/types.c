#include "cc.h"

struct type *ty_void;
struct type *ty_char;
struct type *ty_schar;
struct type *ty_short;
struct type *ty_ushort;
struct type *ty_int;
struct type *ty_uint;
struct type *ty_long;
struct type *ty_ulong;

struct obj *globals;
struct sym *syms;
struct sym *sym_tail;
int unit_id;

struct scope *scope;
int locals_size;

struct type *new_type(int kind, int size, int align, bool uns)
{
	struct type *t = xcalloc(1, sizeof(*t));

	t->kind = kind;
	t->size = size;
	t->align = align;
	t->is_unsigned = uns;
	t->is_complete = true;
	t->array_len = -1;
	return t;
}

struct type *ty_ptr(struct type *base)
{
	struct type *t = new_type(TY_PTR, 8, 8, true);

	t->base = base;
	return t;
}

struct type *ty_array(struct type *base, int len)
{
	struct type *t = new_type(TY_ARRAY, 0, base->align, false);

	t->base = base;
	t->array_len = len;
	if (len >= 0)
		t->size = base->size * len;
	return t;
}

struct type *ty_func(struct type *ret, struct param *params, int n,
		     bool variadic)
{
	struct type *t = new_type(TY_FUNC, 8, 8, false);

	t->base = ret;
	t->params = params;
	t->nparams = n;
	t->variadic = variadic;
	return t;
}

struct type *new_aggregate_type(int kind, const char *tag)
{
	struct type *t = new_type(kind, 0, 1, false);

	t->is_complete = false;
	t->tag = tag ? xstrdup(tag) : NULL;
	return t;
}

void init_types(void)
{
	ty_void = new_type(TY_VOID, 1, 1, false);
	ty_char = new_type(TY_CHAR, 1, 1, true);
	ty_schar = new_type(TY_CHAR, 1, 1, false);
	ty_short = new_type(TY_SHORT, 2, 2, false);
	ty_ushort = new_type(TY_SHORT, 2, 2, true);
	ty_int = new_type(TY_INT, 4, 4, false);
	ty_uint = new_type(TY_INT, 4, 4, true);
	ty_long = new_type(TY_LONG, 8, 8, false);
	ty_ulong = new_type(TY_LONG, 8, 8, true);
}

bool is_integer(struct type *t)
{
	return t->kind == TY_CHAR || t->kind == TY_SHORT || t->kind == TY_INT ||
	       t->kind == TY_LONG;
}

bool is_ptr_like(struct type *t)
{
	return t->kind == TY_PTR || t->kind == TY_ARRAY;
}

bool is_aggregate(struct type *t)
{
	return t->kind == TY_STRUCT || t->kind == TY_UNION;
}

bool is_scalar(struct type *t)
{
	return is_integer(t) || t->kind == TY_PTR;
}

struct type *decay(struct type *t)
{
	if (t->kind == TY_ARRAY)
		return ty_ptr(t->base);
	if (t->kind == TY_FUNC)
		return ty_ptr(t);
	return t;
}

struct node *new_node(int kind)
{
	struct node *n = xcalloc(1, sizeof(*n));

	n->kind = kind;
	return n;
}

struct node *new_num(long val)
{
	struct node *n = new_node(ND_NUM);

	n->val = val;
	n->ty = (val >= -2147483648L && val <= 2147483647L) ? ty_int : ty_long;
	return n;
}

struct sym *new_sym(const char *name, int sec, int size, int align)
{
	struct sym *s = xcalloc(1, sizeof(*s));

	s->name = xstrdup(name);
	s->sec = sec;
	s->size = size;
	s->align = align ? align : 1;
	if (sym_tail)
		sym_tail->next = s;
	else
		syms = s;
	sym_tail = s;
	return s;
}

void make_local_sym(struct sym *s)
{
	char *p;
	size_t n;

	if (s->is_static)
		return;
	s->is_static = true;
	n = strlen(s->name) + 40;
	p = xmalloc(n);
	snprintf(p, n, "%s.__u%d", s->name, unit_id);
	free(s->name);
	s->name = p;
}

struct obj *find_global(const char *name)
{
	struct obj *o;

	for (o = globals; o; o = o->next) {
		if (!strcmp(o->name, name))
			return o;
	}
	return NULL;
}

struct obj *new_global(const char *name, struct type *ty)
{
	struct obj *o = xcalloc(1, sizeof(*o));

	o->name = xstrdup(name);
	o->ty = ty;
	o->next = globals;
	globals = o;
	return o;
}

void enter_scope(void)
{
	struct scope *s = xcalloc(1, sizeof(*s));

	s->parent = scope;
	scope = s;
}

void leave_scope(void)
{
	if (scope)
		scope = scope->parent;
}

struct obj *find_local(const char *name)
{
	struct scope *s;

	for (s = scope; s; s = s->parent) {
		struct obj *o;

		for (o = s->vars; o; o = o->next) {
			if (!strcmp(o->name, name))
				return o;
		}
	}
	return NULL;
}

struct obj *new_local(const char *name, struct type *ty, bool is_param,
		      int param_index)
{
	struct obj *o = xcalloc(1, sizeof(*o));
	int alloc;

	if (ty->size <= 0)
		fatal("incomplete type for local '%s'", name);
	alloc = ty->size > 8 ? align_to(ty->size, 8) : 8;
	locals_size += alloc;
	o->name = xstrdup(name);
	o->ty = ty;
	o->is_local = true;
	o->is_param = is_param;
	o->param_index = param_index;
	o->offset = locals_size;
	o->next = scope->vars;
	scope->vars = o;
	return o;
}

struct node *new_var_node(struct obj *var)
{
	struct node *n = new_node(ND_VAR);

	n->var = var;
	n->ty = var->ty;
	return n;
}
