#include "cc.h"

/* ------------------------------------------------------------------ */
/* preprocessor                                                        */
/* ------------------------------------------------------------------ */

struct macro {
	char *name;
	char *body;
	bool is_func;
	char **params;
	int nparams;
	bool variadic;
	struct macro *next;
};

struct hide {
	struct macro *m;
	struct hide *next;
};

struct inc_path {
	char *path;
	struct inc_path *next;
};

struct cond {
	bool parent_active;
	bool active;
	bool taken;
	bool seen_else;
	struct cond *prev;
};

static struct macro *macros;
static struct inc_path *inc_paths;
static struct inc_path *inc_tail;
static struct cond *conds;
static int pp_include_depth;

#define PP_MAX_INCLUDE 32
#define PP_MAX_EXPAND 100

static struct macro *find_macro(const char *name)
{
	struct macro *m;

	for (m = macros; m; m = m->next) {
		if (!strcmp(m->name, name))
			return m;
	}
	return NULL;
}

static void free_macro_params(struct macro *m)
{
	int i;

	if (!m->params)
		return;
	for (i = 0; i < m->nparams; i++)
		free(m->params[i]);
	free(m->params);
	m->params = NULL;
	m->nparams = 0;
	m->variadic = false;
}

static void define_macro(const char *name, const char *body)
{
	struct macro *m = find_macro(name);

	if (!m) {
		m = xcalloc(1, sizeof(*m));
		m->name = xstrdup(name);
		m->next = macros;
		macros = m;
	} else {
		free(m->body);
		free_macro_params(m);
	}
	m->is_func = false;
	m->body = xstrdup(body);
}

static void define_func_macro(const char *name, char **params, int nparams,
			      bool variadic, const char *body)
{
	struct macro *m = find_macro(name);

	if (!m) {
		m = xcalloc(1, sizeof(*m));
		m->name = xstrdup(name);
		m->next = macros;
		macros = m;
	} else {
		free(m->body);
		free_macro_params(m);
	}
	m->is_func = true;
	m->params = params;
	m->nparams = nparams;
	m->variadic = variadic;
	m->body = xstrdup(body);
}

static bool body_has_hash(const char *body)
{
	int i = 0;

	while (body[i]) {
		int c = (unsigned char)body[i];

		if (c == '"' || c == '\'') {
			int q = c;

			i++;
			while (body[i]) {
				if (body[i] == '\\' && body[i + 1]) {
					i += 2;
					continue;
				}
				if ((unsigned char)body[i] == q) {
					i++;
					break;
				}
				i++;
			}
			continue;
		}
		if (c == '#')
			return true;
		i++;
	}
	return false;
}

static void undef_macro(const char *name)
{
	struct macro **pp;

	for (pp = &macros; *pp; pp = &(*pp)->next) {
		if (!strcmp((*pp)->name, name)) {
			struct macro *m = *pp;

			*pp = m->next;
			free(m->name);
			free(m->body);
			free_macro_params(m);
			free(m);
			return;
		}
	}
}

void pp_add_inc_path(const char *path)
{
	struct inc_path *p = xcalloc(1, sizeof(*p));

	p->path = xstrdup(path);
	if (inc_tail)
		inc_tail->next = p;
	else
		inc_paths = p;
	inc_tail = p;
}

static void pp_define_cli(const char *arg)
{
	const char *eq = strchr(arg, '=');
	char *name;
	char *body;

	if (eq) {
		name = xstrndup(arg, (size_t)(eq - arg));
		body = xstrdup(eq + 1);
	} else {
		name = xstrdup(arg);
		body = xstrdup("1");
	}
	if (!name[0])
		fatal("bad -D option");
	define_macro(name, body);
	free(name);
	free(body);
}

static void pp_undef_cli(const char *name)
{
	undef_macro(name);
}

struct cli_macro {
	bool undef;
	char *arg;
	struct cli_macro *next;
};

static struct cli_macro *cli_macros;
static struct cli_macro *cli_macros_tail;

void record_cli_macro(bool undef, const char *arg)
{
	struct cli_macro *m = xcalloc(1, sizeof(*m));

	m->undef = undef;
	m->arg = xstrdup(arg);
	if (cli_macros_tail)
		cli_macros_tail->next = m;
	else
		cli_macros = m;
	cli_macros_tail = m;
}

void apply_cli_macros(void)
{
	struct cli_macro *m;

	macros = NULL;
	for (m = cli_macros; m; m = m->next) {
		if (m->undef)
			pp_undef_cli(m->arg);
		else
			pp_define_cli(m->arg);
	}
}

int try_read_file(const char *path, char **out, int *out_len)
{
	int fd = open(path, O_RDONLY);
	long sz;
	long off;
	char *buf;

	if (fd < 0)
		return -1;
	sz = lseek(fd, 0, SEEK_END);
	if (sz < 0) {
		close(fd);
		return -1;
	}
	if (lseek(fd, 0, SEEK_SET) < 0) {
		close(fd);
		return -1;
	}
	buf = xmalloc((size_t)sz + 1);
	off = 0;
	while (off < sz) {
		ssize_t n = read(fd, buf + off, (size_t)(sz - off));

		if (n <= 0) {
			close(fd);
			free(buf);
			return -1;
		}
		off += (long)n;
	}
	buf[sz] = '\0';
	close(fd);
	*out = buf;
	*out_len = (int)sz;
	return 0;
}

static char *join_path(const char *dir, const char *name)
{
	size_t dl = strlen(dir);
	size_t nl = strlen(name);
	char *p = xmalloc(dl + nl + 2);

	memcpy(p, dir, dl);
	p[dl] = '/';
	memcpy(p + dl + 1, name, nl + 1);
	return p;
}

static char *path_dirname(const char *path)
{
	const char *slash = strrchr(path, '/');

	if (!slash)
		return xstrdup(".");
	if (slash == path)
		return xstrdup("/");
	return xstrndup(path, (size_t)(slash - path));
}

static bool is_space_ch(int c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static char *splice_lines(const char *src, int len, int *out_len)
{
	struct buf out;
	int i;

	buf_init(&out);
	for (i = 0; i < len; i++) {
		if (src[i] == '\\' && i + 1 < len && src[i + 1] == '\n') {
			i++;
			continue;
		}
		if (src[i] == '\\' && i + 2 < len && src[i + 1] == '\r' &&
		    src[i + 2] == '\n') {
			i += 2;
			continue;
		}
		buf_byte(&out, (unsigned char)src[i]);
	}
	*out_len = out.len;
	return (char *)out.data;
}

static char *strip_comments(const char *src, int len, int *out_len)
{
	struct buf out;
	int i = 0;

	buf_init(&out);
	while (i < len) {
		int c = (unsigned char)src[i];

		if (c == '/' && i + 1 < len && src[i + 1] == '/') {
			i += 2;
			while (i < len && src[i] != '\n')
				i++;
			continue;
		}
		if (c == '/' && i + 1 < len && src[i + 1] == '*') {
			i += 2;
			while (i < len && !(src[i] == '*' && i + 1 < len &&
					    src[i + 1] == '/')) {
				if (src[i] == '\n')
					buf_byte(&out, '\n');
				i++;
			}
			if (i >= len)
				fatal("unterminated block comment");
			i += 2;
			continue;
		}
		if (c == '"' || c == '\'') {
			int q = c;

			buf_byte(&out, c);
			i++;
			while (i < len) {
				int d = (unsigned char)src[i];

				buf_byte(&out, d);
				i++;
				if (d == '\\' && i < len) {
					buf_byte(&out, (unsigned char)src[i]);
					i++;
					continue;
				}
				if (d == q)
					break;
				if (d == '\n')
					break;
			}
			continue;
		}
		buf_byte(&out, c);
		i++;
	}
	*out_len = out.len;
	return (char *)out.data;
}

static bool pp_active(void)
{
	return !conds || conds->active;
}

static char *trim_right(char *s)
{
	char *e = s + strlen(s);

	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'))
		*--e = '\0';
	return s;
}

static void expand_func_macro(struct buf *out, struct macro *m,
			      const char *text, int *pos, struct hide *hide,
			      int depth);

static void expand_into(struct buf *out, const char *text, struct hide *hide,
			int depth)
{
	int i = 0;
	int n = (int)strlen(text);

	if (depth > PP_MAX_EXPAND)
		fatal("macro expansion too deep");
	while (i < n) {
		int c = (unsigned char)text[i];

		if (c == '"' || c == '\'') {
			int q = c;

			buf_byte(out, c);
			i++;
			while (i < n) {
				int d = (unsigned char)text[i];

				buf_byte(out, d);
				i++;
				if (d == '\\' && i < n) {
					buf_byte(out, (unsigned char)text[i]);
					i++;
					continue;
				}
				if (d == q)
					break;
			}
			continue;
		}
		if (starts_ident(c)) {
			int start = i;
			char *name;
			struct macro *m;
			struct hide *h;
			bool hidden = false;

			while (i < n && is_ident_char((unsigned char)text[i]))
				i++;
			name = xstrndup(text + start, (size_t)(i - start));
			m = find_macro(name);
			for (h = hide; h; h = h->next) {
				if (h->m == m) {
					hidden = true;
					break;
				}
			}
			if (m && !hidden) {
				if (m->is_func) {
					int j = i;

					while (j < n &&
					       is_space_ch(
						       (unsigned char)text[j]))
						j++;
					if (j < n && text[j] == '(') {
						expand_func_macro(out, m, text,
								  &j, hide,
								  depth);
						i = j;
					} else {
						for (j = start; j < i; j++)
							buf_byte(
								out,
								(unsigned char)
									text[j]);
					}
				} else {
					struct hide h2;

					h2.m = m;
					h2.next = hide;
					expand_into(out, m->body, &h2,
						    depth + 1);
				}
			} else {
				int j;

				for (j = start; j < i; j++)
					buf_byte(out, (unsigned char)text[j]);
			}
			free(name);
			continue;
		}
		buf_byte(out, c);
		i++;
	}
}

struct marg {
	char *raw;
	char *exp;
};

static int macro_param_index(struct macro *m, const char *name)
{
	int i;

	for (i = 0; i < m->nparams; i++) {
		if (!strcmp(m->params[i], name))
			return i;
	}
	return -1;
}

static char *trim_slice(const char *s, int len)
{
	int a = 0;
	int b = len;

	while (a < b && is_space_ch((unsigned char)s[a]))
		a++;
	while (b > a && is_space_ch((unsigned char)s[b - 1]))
		b--;
	return xstrndup(s + a, (size_t)(b - a));
}

static void expand_func_macro(struct buf *out, struct macro *m,
			      const char *text, int *pos, struct hide *hide,
			      int depth)
{
	int n = (int)strlen(text);
	int p = *pos + 1;
	struct marg *args = NULL;
	int nargs = 0;
	int cap = 0;
	int depthp = 1;
	int t = p;
	int i;
	char **exp;
	char *va;
	struct buf sub;
	struct hide h2;

	while (t < n && is_space_ch((unsigned char)text[t]))
		t++;
	if (t < n && text[t] == ')') {
		p = t + 1;
	} else {
		for (;;) {
			int start = p;
			int c;

			while (p < n) {
				c = (unsigned char)text[p];
				if (c == '"' || c == '\'') {
					int q = c;

					p++;
					while (p < n) {
						int d = (unsigned char)text[p];

						p++;
						if (d == '\\' && p < n) {
							p++;
							continue;
						}
						if (d == q)
							break;
					}
					continue;
				}
				if (c == '(') {
					depthp++;
					p++;
					continue;
				}
				if (c == ')') {
					depthp--;
					if (depthp == 0)
						break;
					p++;
					continue;
				}
				if (c == ',' && depthp == 1)
					break;
				p++;
			}
			if (p >= n)
				fatal("unterminated macro invocation of '%s'",
				      m->name);
			if (nargs == cap) {
				cap = cap ? cap * 2 : 8;
				args = xrealloc(args,
						(size_t)cap * sizeof(*args));
			}
			args[nargs].raw = trim_slice(text + start, p - start);
			args[nargs].exp = NULL;
			nargs++;
			if (text[p] == ')') {
				p++;
				break;
			}
			p++; /* comma */
		}
	}
	if (nargs == 0 && m->nparams == 1) {
		if (nargs == cap) {
			cap = cap ? cap * 2 : 8;
			args = xrealloc(args, (size_t)cap * sizeof(*args));
		}
		args[0].raw = xstrdup("");
		args[0].exp = NULL;
		nargs = 1;
	}
	if (m->variadic) {
		if (nargs < m->nparams)
			fatal("macro '%s' expects at least %d arguments",
			      m->name, m->nparams);
	} else if (nargs != m->nparams) {
		fatal("macro '%s' expects %d arguments, got %d", m->name,
		      m->nparams, nargs);
	}

	exp = xcalloc((size_t)(nargs > 0 ? nargs : 1), sizeof(*exp));
	for (i = 0; i < nargs; i++) {
		struct buf b;

		buf_init(&b);
		expand_into(&b, args[i].raw, hide, depth + 1);
		buf_byte(&b, 0);
		exp[i] = (char *)b.data;
	}

	va = xstrdup("");
	if (m->variadic && nargs > m->nparams) {
		struct buf vb;

		buf_init(&vb);
		for (i = m->nparams; i < nargs; i++) {
			size_t l = strlen(exp[i]);

			if (i > m->nparams)
				buf_byte(&vb, ',');
			buf_grow(&vb, (int)l);
			memcpy(vb.data + vb.len, exp[i], l);
			vb.len += (int)l;
		}
		buf_byte(&vb, 0);
		va = (char *)vb.data;
	}

	buf_init(&sub);
	{
		int bl = (int)strlen(m->body);
		int k = 0;

		while (k < bl) {
			int c = (unsigned char)m->body[k];

			if (c == '"' || c == '\'') {
				int q = c;

				buf_byte(&sub, c);
				k++;
				while (k < bl) {
					int d = (unsigned char)m->body[k];

					buf_byte(&sub, d);
					k++;
					if (d == '\\' && k < bl) {
						buf_byte(&sub,
							 (unsigned char)
								 m->body[k]);
						k++;
						continue;
					}
					if (d == q)
						break;
				}
				continue;
			}
			if (starts_ident(c)) {
				int st = k;
				char *name;
				int pi;

				while (k < bl &&
				       is_ident_char((unsigned char)m->body[k]))
					k++;
				name = xstrndup(m->body + st, (size_t)(k - st));
				pi = macro_param_index(m, name);
				if (pi >= 0) {
					size_t l = strlen(exp[pi]);

					buf_grow(&sub, (int)l);
					memcpy(sub.data + sub.len, exp[pi], l);
					sub.len += (int)l;
				} else if (m->variadic &&
					   !strcmp(name, "__VA_ARGS__")) {
					size_t l = strlen(va);

					buf_grow(&sub, (int)l);
					memcpy(sub.data + sub.len, va, l);
					sub.len += (int)l;
				} else {
					int j;

					for (j = st; j < k; j++)
						buf_byte(&sub,
							 (unsigned char)
								 m->body[j]);
				}
				free(name);
				continue;
			}
			if (c == '#')
				fatal("stringification/token pasting is not supported");
			buf_byte(&sub, c);
			k++;
		}
	}
	buf_byte(&sub, 0);
	h2.m = m;
	h2.next = hide;
	expand_into(out, (char *)sub.data, &h2, depth + 1);

	for (i = 0; i < nargs; i++) {
		free(args[i].raw);
		free(exp[i]);
	}
	free(args);
	free(exp);
	free(va);
	free(sub.data);
	*pos = p;
}

/* ---- #if expression evaluator ---- */

enum {
	PP_EOF = 0,
	PP_NUM,
	PP_IDENT,
	PP_OP,
};

struct pp_tok {
	int kind;
	long val;
	const char *s;
	int len;
};

struct pp_parser {
	const char *src;
	int pos;
	struct pp_tok tok;
	int depth;
};

static long pp_or(struct pp_parser *p, bool eval);

static void pp_next(struct pp_parser *p)
{
	const char *s = p->src;
	int n = (int)strlen(s);
	int i = p->pos;
	static const char *const ops[] = {
		"&&", "||", "==", "!=", "<=", ">=", "<<", ">>", "+", "-", "*",
		"/",  "%",  "!",  "~",	"&",  "|",  "^",  "<",	">", "(", ")",
	};

	while (i < n && is_space_ch((unsigned char)s[i]))
		i++;
	if (i >= n) {
		p->tok.kind = PP_EOF;
		p->tok.s = "";
		p->tok.len = 0;
		p->pos = i;
		return;
	}
	if (s[i] >= '0' && s[i] <= '9') {
		char *end;
		unsigned long v = strtoul(s + i, &end, 0);

		i = (int)(end - s);
		while (i < n && (s[i] == 'u' || s[i] == 'U' || s[i] == 'l' ||
				 s[i] == 'L'))
			i++;
		p->tok.kind = PP_NUM;
		p->tok.val = (long)v;
		p->tok.s = s;
		p->tok.len = 0;
		p->pos = i;
		return;
	}
	if (starts_ident((unsigned char)s[i])) {
		int st = i;

		while (i < n && is_ident_char((unsigned char)s[i]))
			i++;
		p->tok.kind = PP_IDENT;
		p->tok.s = s + st;
		p->tok.len = i - st;
		p->pos = i;
		return;
	}
	if (s[i] == '\'') {
		int v;

		i++;
		if (i < n && s[i] == '\\') {
			i++;
			if (i >= n)
				fatal("bad character constant in #if");
			switch (s[i]) {
			case 'n':
				v = '\n';
				break;
			case 't':
				v = '\t';
				break;
			case 'r':
				v = '\r';
				break;
			case '0':
				v = 0;
				break;
			case '\\':
				v = '\\';
				break;
			case '\'':
				v = '\'';
				break;
			default:
				v = (unsigned char)s[i];
				break;
			}
			i++;
		} else if (i < n) {
			v = (unsigned char)s[i++];
		} else {
			fatal("bad character constant in #if");
			v = 0;
		}
		if (i >= n || s[i] != '\'')
			fatal("bad character constant in #if");
		i++;
		p->tok.kind = PP_NUM;
		p->tok.val = v;
		p->tok.s = s;
		p->tok.len = 0;
		p->pos = i;
		return;
	}
	for (size_t k = 0; k < sizeof(ops) / sizeof(ops[0]); k++) {
		int l = (int)strlen(ops[k]);

		if (i + l <= n && !strncmp(s + i, ops[k], (size_t)l)) {
			p->tok.kind = PP_OP;
			p->tok.s = s + i;
			p->tok.len = l;
			p->pos = i + l;
			return;
		}
	}
	fatal("bad token in #if expression");
}

static bool tok_op(const struct pp_tok *t, const char *op)
{
	int n = (int)strlen(op);

	return t->kind == PP_OP && t->len == n && !strncmp(t->s, op, (size_t)n);
}

static bool tok_ident(const struct pp_tok *t, const char *name)
{
	int n = (int)strlen(name);

	return t->kind == PP_IDENT && t->len == n &&
	       !strncmp(t->s, name, (size_t)n);
}

static bool pp_accept(struct pp_parser *p, const char *op)
{
	if (!tok_op(&p->tok, op))
		return false;
	pp_next(p);
	return true;
}

static void pp_expect(struct pp_parser *p, const char *op)
{
	if (!pp_accept(p, op))
		fatal("expected '%s' in #if expression", op);
}

static long pp_eval_text(const char *text, int depth);

static long pp_primary(struct pp_parser *p, bool eval)
{
	if (p->tok.kind == PP_NUM) {
		long v = p->tok.val;

		pp_next(p);
		return eval ? v : 0;
	}
	if (p->tok.kind == PP_IDENT) {
		if (tok_ident(&p->tok, "defined")) {
			bool paren = false;
			char *name;
			long v;

			pp_next(p);
			if (pp_accept(p, "("))
				paren = true;
			if (p->tok.kind != PP_IDENT)
				fatal("expected identifier after defined");
			name = xstrndup(p->tok.s, (size_t)p->tok.len);
			v = find_macro(name) ? 1 : 0;
			free(name);
			pp_next(p);
			if (paren)
				pp_expect(p, ")");
			return eval ? v : 0;
		}
		{
			char *name = xstrndup(p->tok.s, (size_t)p->tok.len);
			struct macro *m = find_macro(name);
			long v = 0;

			free(name);
			pp_next(p);
			if (m && eval)
				v = pp_eval_text(m->body, p->depth + 1);
			return eval ? v : 0;
		}
	}
	if (pp_accept(p, "(")) {
		long v = pp_or(p, eval);

		pp_expect(p, ")");
		return v;
	}
	fatal("expected expression in #if");
	return 0;
}

static long pp_unary(struct pp_parser *p, bool eval)
{
	if (pp_accept(p, "!")) {
		long v = pp_unary(p, eval);

		return eval ? !v : 0;
	}
	if (pp_accept(p, "~")) {
		long v = pp_unary(p, eval);

		return eval ? ~v : 0;
	}
	if (pp_accept(p, "+"))
		return pp_unary(p, eval);
	if (pp_accept(p, "-")) {
		long v = pp_unary(p, eval);

		return eval ? -v : 0;
	}
	return pp_primary(p, eval);
}

static long pp_mul(struct pp_parser *p, bool eval)
{
	long v = pp_unary(p, eval);

	for (;;) {
		if (pp_accept(p, "*")) {
			long r = pp_unary(p, eval);

			if (eval)
				v = v * r;
		} else if (pp_accept(p, "/")) {
			long r = pp_unary(p, eval);

			if (eval) {
				if (r == 0)
					fatal("division by zero in #if");
				v = v / r;
			}
		} else if (pp_accept(p, "%")) {
			long r = pp_unary(p, eval);

			if (eval) {
				if (r == 0)
					fatal("division by zero in #if");
				v = v % r;
			}
		} else {
			return eval ? v : 0;
		}
	}
}

static long pp_add(struct pp_parser *p, bool eval)
{
	long v = pp_mul(p, eval);

	for (;;) {
		if (pp_accept(p, "+")) {
			long r = pp_mul(p, eval);

			if (eval)
				v = v + r;
		} else if (pp_accept(p, "-")) {
			long r = pp_mul(p, eval);

			if (eval)
				v = v - r;
		} else {
			return eval ? v : 0;
		}
	}
}

static long pp_shift(struct pp_parser *p, bool eval)
{
	long v = pp_add(p, eval);

	for (;;) {
		if (pp_accept(p, "<<")) {
			long r = pp_add(p, eval);

			if (eval)
				v = v << r;
		} else if (pp_accept(p, ">>")) {
			long r = pp_add(p, eval);

			if (eval)
				v = v >> r;
		} else {
			return eval ? v : 0;
		}
	}
}

static long pp_rel(struct pp_parser *p, bool eval)
{
	long v = pp_shift(p, eval);

	for (;;) {
		if (pp_accept(p, "<")) {
			long r = pp_shift(p, eval);

			if (eval)
				v = v < r;
		} else if (pp_accept(p, "<=")) {
			long r = pp_shift(p, eval);

			if (eval)
				v = v <= r;
		} else if (pp_accept(p, ">")) {
			long r = pp_shift(p, eval);

			if (eval)
				v = v > r;
		} else if (pp_accept(p, ">=")) {
			long r = pp_shift(p, eval);

			if (eval)
				v = v >= r;
		} else {
			return eval ? v : 0;
		}
	}
}

static long pp_eq(struct pp_parser *p, bool eval)
{
	long v = pp_rel(p, eval);

	for (;;) {
		if (pp_accept(p, "==")) {
			long r = pp_rel(p, eval);

			if (eval)
				v = v == r;
		} else if (pp_accept(p, "!=")) {
			long r = pp_rel(p, eval);

			if (eval)
				v = v != r;
		} else {
			return eval ? v : 0;
		}
	}
}

static long pp_band(struct pp_parser *p, bool eval)
{
	long v = pp_eq(p, eval);

	while (pp_accept(p, "&")) {
		long r = pp_eq(p, eval);

		if (eval)
			v = v & r;
	}
	return eval ? v : 0;
}

static long pp_bxor(struct pp_parser *p, bool eval)
{
	long v = pp_band(p, eval);

	while (pp_accept(p, "^")) {
		long r = pp_band(p, eval);

		if (eval)
			v = v ^ r;
	}
	return eval ? v : 0;
}

static long pp_bor(struct pp_parser *p, bool eval)
{
	long v = pp_bxor(p, eval);

	while (pp_accept(p, "|")) {
		long r = pp_bxor(p, eval);

		if (eval)
			v = v | r;
	}
	return eval ? v : 0;
}

static long pp_and(struct pp_parser *p, bool eval)
{
	long v = pp_bor(p, eval);

	while (pp_accept(p, "&&")) {
		bool ev = eval && v != 0;
		long r = pp_bor(p, ev);

		if (eval)
			v = (v != 0 && r != 0);
	}
	return eval ? v : 0;
}

static long pp_or(struct pp_parser *p, bool eval)
{
	long v = pp_and(p, eval);

	while (pp_accept(p, "||")) {
		bool ev = eval && v == 0;
		long r = pp_and(p, ev);

		if (eval)
			v = (v != 0 || r != 0);
	}
	return eval ? v : 0;
}

static long pp_eval_text(const char *text, int depth)
{
	struct pp_parser p;

	if (depth > PP_MAX_EXPAND)
		fatal("#if macro expansion too deep");
	p.src = text;
	p.pos = 0;
	p.depth = depth;
	pp_next(&p);
	{
		long v = pp_or(&p, true);

		if (p.tok.kind != PP_EOF)
			fatal("unexpected token in #if expression");
		return v;
	}
}

/* ---- directive processing ---- */

static void push_cond(bool parent_active, bool active)
{
	struct cond *c = xcalloc(1, sizeof(*c));

	c->parent_active = parent_active;
	c->active = active;
	c->taken = active;
	c->prev = conds;
	conds = c;
}

static int load_include(const char *name, bool quoted, const char *cur_dir,
			char **text, int *len, char **resolved)
{
	if (name[0] == '/') {
		if (try_read_file(name, text, len) == 0) {
			*resolved = xstrdup(name);
			return 0;
		}
		return -1;
	}
	if (quoted) {
		char *p = join_path(cur_dir, name);

		if (try_read_file(p, text, len) == 0) {
			*resolved = p;
			return 0;
		}
		free(p);
	}
	for (struct inc_path *ip = inc_paths; ip; ip = ip->next) {
		char *p = join_path(ip->path, name);

		if (try_read_file(p, text, len) == 0) {
			*resolved = p;
			return 0;
		}
		free(p);
	}
	return -1;
}

static void pp_process(const char *path, const char *src, int len,
		       struct buf *out, struct cond *floor);

static void pp_include(const char *path, int line_no, char *arg,
		       struct buf *out)
{
	char *p = arg;
	bool quoted;
	char close;
	char *name;
	char *text;
	int tlen;
	char *resolved;
	char *cur_dir;
	int err;

	while (*p == ' ' || *p == '\t')
		p++;
	if (*p != '"' && *p != '<')
		fatal("%s:%d: malformed #include", path, line_no);
	quoted = *p == '"';
	close = quoted ? '"' : '>';
	p++;
	name = p;
	while (*p && *p != close)
		p++;
	if (*p != close)
		fatal("%s:%d: malformed #include", path, line_no);
	*p = '\0';
	cur_dir = path_dirname(path);
	err = load_include(name, quoted, cur_dir, &text, &tlen, &resolved);
	free(cur_dir);
	if (err < 0)
		fatal("%s:%d: cannot find include '%s'", path, line_no, name);
	if (++pp_include_depth > PP_MAX_INCLUDE)
		fatal("%s:%d: include nesting too deep", path, line_no);
	pp_process(resolved, text, tlen, out, conds);
	pp_include_depth--;
	free(resolved);
	free(text);
}

static void pp_process(const char *path, const char *src, int len,
		       struct buf *out, struct cond *floor)
{
	char *joined;
	char *clean;
	int jlen;
	int clen;
	int pos = 0;
	int line_no = 1;
	char *cur_dir = path_dirname(path);

	joined = splice_lines(src, len, &jlen);
	clean = strip_comments(joined, jlen, &clen);
	free(joined);

	while (pos < clen) {
		int start = pos;
		int line_len;
		bool had_nl;
		char *line;
		char *p;

		while (pos < clen && clean[pos] != '\n')
			pos++;
		line_len = pos - start;
		had_nl = pos < clen;
		if (had_nl)
			pos++;
		line = xstrndup(clean + start, (size_t)line_len);
		p = line;
		while (*p == ' ' || *p == '\t' || *p == '\r')
			p++;

		if (*p == '#') {
			char *d = p + 1;
			char *name;
			int name_len;
			char *arg;

			while (*d == ' ' || *d == '\t')
				d++;
			name = d;
			while (*d && is_ident_char((unsigned char)*d))
				d++;
			name_len = (int)(d - name);
			arg = d;
			while (*arg == ' ' || *arg == '\t')
				arg++;
			trim_right(arg);

			if (name_len == 7 && !strncmp(name, "include", 7)) {
				if (pp_active())
					pp_include(path, line_no, arg, out);
			} else if (name_len == 6 &&
				   !strncmp(name, "define", 6)) {
				if (pp_active()) {
					char *n = arg;
					char *ne;
					char *body;
					char *mname;

					while (*n == ' ' || *n == '\t')
						n++;
					if (!starts_ident((unsigned char)*n))
						fatal("%s:%d: bad #define",
						      path, line_no);
					ne = n;
					while (is_ident_char(
						(unsigned char)*ne))
						ne++;
					if (*ne == '(') {
						char **params = NULL;
						int np = 0;
						int cap = 0;
						bool variadic = false;
						char *q = ne + 1;

						while (*q == ' ' || *q == '\t')
							q++;
						if (*q == ')') {
							q++;
						} else {
							for (;;) {
								while (*q == ' ' ||
								       *q == '\t')
									q++;
								if (!strncmp(
									    q,
									    "...",
									    3)) {
									variadic =
										true;
									q += 3;
									while (*q == ' ' ||
									       *q == '\t')
										q++;
									if (*q !=
									    ')')
										fatal("%s:%d: bad macro parameter list",
										      path,
										      line_no);
									q++;
									break;
								}
								if (!starts_ident((
									    unsigned char)*q))
									fatal("%s:%d: bad macro parameter list",
									      path,
									      line_no);
								{
									char *st =
										q;

									while (is_ident_char(
										(unsigned char)*q))
										q++;
									if (np ==
									    cap) {
										cap = cap ? cap * 2 :
											    8;
										params = xrealloc(
											params,
											(size_t)cap *
												sizeof(*params));
									}
									params[np++] = xstrndup(
										st,
										(size_t)(q -
											 st));
								}
								while (*q == ' ' ||
								       *q == '\t')
									q++;
								if (*q == ',') {
									q++;
									continue;
								}
								if (*q == ')') {
									q++;
									break;
								}
								fatal("%s:%d: bad macro parameter list",
								      path,
								      line_no);
							}
						}
						body = q;
						while (*body == ' ' ||
						       *body == '\t')
							body++;
						if (body_has_hash(body))
							fatal("%s:%d: stringification/token pasting is not supported",
							      path, line_no);
						mname = xstrndup(
							n, (size_t)(ne - n));
						define_func_macro(mname, params,
								  np, variadic,
								  body);
						free(mname);
					} else {
						body = ne;
						while (*body == ' ' ||
						       *body == '\t')
							body++;
						mname = xstrndup(
							n, (size_t)(ne - n));
						define_macro(mname, body);
						free(mname);
					}
				}
			} else if (name_len == 5 &&
				   !strncmp(name, "undef", 5)) {
				if (pp_active()) {
					char *n = arg;
					char *ne;
					char *mname;

					while (*n == ' ' || *n == '\t')
						n++;
					ne = n;
					while (is_ident_char(
						(unsigned char)*ne))
						ne++;
					mname = xstrndup(n, (size_t)(ne - n));
					undef_macro(mname);
					free(mname);
				}
			} else if (name_len == 5 &&
				   !strncmp(name, "ifdef", 5)) {
				bool parent = pp_active();
				char *n = arg;
				char *ne;
				char *mname;
				bool val;

				while (*n == ' ' || *n == '\t')
					n++;
				ne = n;
				while (is_ident_char((unsigned char)*ne))
					ne++;
				mname = xstrndup(n, (size_t)(ne - n));
				val = find_macro(mname) != NULL;
				free(mname);
				push_cond(parent, parent && val);
			} else if (name_len == 6 &&
				   !strncmp(name, "ifndef", 6)) {
				bool parent = pp_active();
				char *n = arg;
				char *ne;
				char *mname;
				bool val;

				while (*n == ' ' || *n == '\t')
					n++;
				ne = n;
				while (is_ident_char((unsigned char)*ne))
					ne++;
				mname = xstrndup(n, (size_t)(ne - n));
				val = find_macro(mname) == NULL;
				free(mname);
				push_cond(parent, parent && val);
			} else if (name_len == 2 && !strncmp(name, "if", 2)) {
				bool parent = pp_active();
				long v = parent ? pp_eval_text(arg, 0) : 0;

				push_cond(parent, parent && v != 0);
			} else if (name_len == 4 && !strncmp(name, "elif", 4)) {
				if (!conds || conds == floor)
					fatal("%s:%d: unexpected #elif", path,
					      line_no);
				if (conds->seen_else)
					fatal("%s:%d: #elif after #else", path,
					      line_no);
				if (conds->parent_active && !conds->taken) {
					long v = pp_eval_text(arg, 0);

					conds->active = v != 0;
					if (v)
						conds->taken = true;
				} else {
					conds->active = false;
				}
			} else if (name_len == 4 && !strncmp(name, "else", 4)) {
				if (!conds || conds == floor)
					fatal("%s:%d: unexpected #else", path,
					      line_no);
				if (conds->seen_else)
					fatal("%s:%d: duplicate #else", path,
					      line_no);
				conds->seen_else = true;
				conds->active = conds->parent_active &&
						!conds->taken;
				conds->taken = true;
			} else if (name_len == 5 &&
				   !strncmp(name, "endif", 5)) {
				if (!conds || conds == floor)
					fatal("%s:%d: unexpected #endif", path,
					      line_no);
				conds = conds->prev;
			} else if (name_len == 5 &&
				   !strncmp(name, "error", 5)) {
				if (pp_active())
					fatal("%s:%d: #error %s", path, line_no,
					      arg);
			} else if (name_len == 7 &&
				   !strncmp(name, "warning", 7)) {
				if (pp_active())
					fprintf(stderr,
						"cc: %s:%d: warning: %s\n",
						path, line_no, arg);
			} else if (name_len == 6 &&
				   !strncmp(name, "pragma", 6)) {
				/* ignored */
			} else if (name_len == 4 && !strncmp(name, "line", 4)) {
				/* ignored */
			}
		} else if (pp_active()) {
			expand_into(out, line, NULL, 0);
			buf_byte(out, '\n');
		}
		free(line);
		line_no++;
	}

	if (conds != floor)
		fatal("%s: unterminated conditional directive", path);
	free(cur_dir);
	free(clean);
}

char *preprocess(const char *path, const char *src, int len, int *out_len)
{
	struct buf out;

	buf_init(&out);
	pp_process(path, src, len, &out, NULL);
	*out_len = out.len;
	buf_byte(&out, 0);
	return (char *)out.data;
}

void reset_pp_state(void)
{
	pp_include_depth = 0;
}
