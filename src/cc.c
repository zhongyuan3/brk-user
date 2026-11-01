/*
 * cc - a very small C compiler for BRK/RISC-V64.
 *
 * This compiler is deliberately self contained: it parses one translation
 * unit and writes a static ELF64 executable directly.  There is no assembler,
 * no linker and no dependency on libuser at runtime.
 *
 * Supported subset (first cut):
 *   - void/char/short/int/long, signed/unsigned, pointers, arrays, functions
 *   - block/if/else/while/for/do-while/return/break/continue
 *   - the usual integer/pointer operators, casts and sizeof
 *   - global and local variables, string literals
 *   - up to 8 function arguments
 *
 * Builtins: write/read/open/close/exit/putchar/puts.
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* basic utilities                                                     */
/* ------------------------------------------------------------------ */

static void fatal(const char *fmt, ...)
	__attribute__((noreturn, format(printf, 1, 2)));

static void fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	fprintf(stderr, "cc: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

static void *xmalloc(size_t n)
{
	void *p = malloc(n ? n : 1);

	if (!p)
		fatal("out of memory");
	return p;
}

static void *xcalloc(size_t n, size_t sz)
{
	void *p = calloc(n ? n : 1, sz ? sz : 1);

	if (!p)
		fatal("out of memory");
	return p;
}

static void *xrealloc(void *p, size_t n)
{
	void *q = realloc(p, n ? n : 1);

	if (!q)
		fatal("out of memory");
	return q;
}

static char *xstrndup(const char *s, size_t n)
{
	char *p = xmalloc(n + 1);

	memcpy(p, s, n);
	p[n] = '\0';
	return p;
}

static char *xstrdup(const char *s)
{
	return xstrndup(s, strlen(s));
}

static int align_to(int n, int a)
{
	return (n + a - 1) / a * a;
}

struct buf {
	unsigned char *data;
	int len;
	int cap;
};

static void buf_init(struct buf *b)
{
	b->len = 0;
	b->cap = 256;
	b->data = xmalloc((size_t)b->cap);
}

static void buf_grow(struct buf *b, int need)
{
	if (b->len + need <= b->cap)
		return;
	while (b->cap < b->len + need)
		b->cap *= 2;
	b->data = xrealloc(b->data, (size_t)b->cap);
}

static void buf_byte(struct buf *b, int c)
{
	buf_grow(b, 1);
	b->data[b->len++] = (unsigned char)c;
}

static void buf_u32(struct buf *b, unsigned int v)
{
	buf_byte(b, (int)(v & 0xff));
	buf_byte(b, (int)((v >> 8) & 0xff));
	buf_byte(b, (int)((v >> 16) & 0xff));
	buf_byte(b, (int)((v >> 24) & 0xff));
}

static void buf_u64(struct buf *b, unsigned long v)
{
	int i;

	for (i = 0; i < 8; i++)
		buf_byte(b, (int)((v >> (i * 8)) & 0xff));
}

static void buf_pad(struct buf *b, int n)
{
	while (n-- > 0)
		buf_byte(b, 0);
}

static int buf_align(struct buf *b, int a)
{
	int pad = align_to(b->len, a) - b->len;

	buf_pad(b, pad);
	return pad;
}

/* ------------------------------------------------------------------ */
/* lexer                                                               */
/* ------------------------------------------------------------------ */

enum {
	TK_EOF = 0,
	TK_IDENT,
	TK_NUM,
	TK_STR,
	TK_PUNCT,
};

struct token {
	int kind;
	char *text;
	long val;
	unsigned char *str;
	int str_len;
	int line;
	int col;
};

static struct token *tokens;
static int ntokens;
static int token_cap;
static const char *lex_src;
static int lex_pos;
static int lex_len;
static int lex_line = 1;
static int lex_col = 1;

static int peekc(void)
{
	return lex_pos < lex_len ? (unsigned char)lex_src[lex_pos] : -1;
}

static int peekc2(void)
{
	return lex_pos + 1 < lex_len ? (unsigned char)lex_src[lex_pos + 1] : -1;
}

static int advancec(void)
{
	int c = peekc();

	if (c < 0)
		return -1;
	lex_pos++;
	if (c == '\n') {
		lex_line++;
		lex_col = 1;
	} else {
		lex_col++;
	}
	return c;
}

static void push_token(int kind, char *text, long val, int line, int col)
{
	struct token t;

	if (ntokens == token_cap) {
		token_cap = token_cap ? token_cap * 2 : 256;
		tokens = xrealloc(tokens, (size_t)token_cap * sizeof(*tokens));
	}
	memset(&t, 0, sizeof(t));
	t.kind = kind;
	t.text = text;
	t.val = val;
	t.line = line;
	t.col = col;
	tokens[ntokens++] = t;
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int read_escape(void)
{
	int c = advancec();

	if (c < 0)
		return -1;
	switch (c) {
	case 'n':
		return '\n';
	case 't':
		return '\t';
	case 'r':
		return '\r';
	case 'a':
		return '\a';
	case 'b':
		return '\b';
	case 'f':
		return '\f';
	case 'v':
		return '\v';
	case '\\':
		return '\\';
	case '\'':
		return '\'';
	case '"':
		return '"';
	case '?':
		return '?';
	case 'x': {
		int v = 0;
		int d;
		int n = 0;

		while ((d = hexval(peekc())) >= 0) {
			v = v * 16 + d;
			advancec();
			n++;
		}
		if (n == 0)
			fatal("%d:%d: invalid hex escape", lex_line, lex_col);
		return v & 0xff;
	}
	default:
		if (c >= '0' && c <= '7') {
			int v = c - '0';
			int n = 1;

			while (n < 3 && peekc() >= '0' && peekc() <= '7') {
				v = v * 8 + (advancec() - '0');
				n++;
			}
			return v & 0xff;
		}
		return c;
	}
}

static void lex_string(void)
{
	int line = lex_line;
	int col = lex_col;
	struct buf b;
	char *raw;

	advancec(); /* opening quote */
	buf_init(&b);
	for (;;) {
		int c = advancec();

		if (c < 0 || c == '\n')
			fatal("%d:%d: unterminated string literal", line, col);
		if (c == '"')
			break;
		if (c == '\\')
			c = read_escape();
		buf_byte(&b, c);
	}
	buf_byte(&b, 0);
	raw = xstrndup((const char *)b.data, (size_t)b.len);
	push_token(TK_STR, raw, 0, line, col);
	tokens[ntokens - 1].str = (unsigned char *)raw;
	tokens[ntokens - 1].str_len = b.len;
	free(b.data);
}

static void lex_char(void)
{
	int line = lex_line;
	int col = lex_col;
	int c;

	advancec(); /* opening quote */
	if (peekc() < 0)
		fatal("%d:%d: unterminated character literal", line, col);
	if (peekc() == '\\') {
		advancec(); /* consume the backslash */
		c = read_escape();
	} else {
		c = advancec();
	}
	if (advancec() != '\'')
		fatal("%d:%d: malformed character literal", line, col);
	push_token(TK_NUM, xstrdup("0"), (long)(unsigned char)c, line, col);
}

static long lex_number(void)
{
	int line = lex_line;
	int col = lex_col;
	int base = 10;
	unsigned long v = 0;
	int start = lex_pos;
	int c;

	if (peekc() == '0' && (peekc2() == 'x' || peekc2() == 'X')) {
		advancec();
		advancec();
		base = 16;
		while ((c = hexval(peekc())) >= 0) {
			v = v * (unsigned long)base + (unsigned long)c;
			advancec();
		}
	} else {
		if (peekc() == '0')
			base = 8;
		while (peekc() >= '0' && peekc() <= '9') {
			int d = advancec() - '0';

			if (d >= base)
				fatal("%d:%d: invalid digit", line, col);
			v = v * (unsigned long)base + (unsigned long)d;
		}
	}
	while (peekc() == 'u' || peekc() == 'U' || peekc() == 'l' ||
	       peekc() == 'L')
		advancec();
	push_token(TK_NUM, xstrndup(lex_src + start, (size_t)(lex_pos - start)),
		   (long)v, line, col);
	return (long)v;
}

static bool starts_ident(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_char(int c)
{
	return starts_ident(c) || (c >= '0' && c <= '9');
}

static void lex_ident(void)
{
	int line = lex_line;
	int col = lex_col;
	int start = lex_pos;

	while (is_ident_char(peekc()))
		advancec();
	push_token(TK_IDENT,
		   xstrndup(lex_src + start, (size_t)(lex_pos - start)), 0,
		   line, col);
}

static const char *const multi_ops[] = {
	"<<=", ">>=", "...", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>",
	"++",  "--",  "->",  "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
};

static bool lex_operator(void)
{
	int line = lex_line;
	int col = lex_col;
	size_t i;

	for (i = 0; i < sizeof(multi_ops) / sizeof(multi_ops[0]); i++) {
		size_t n = strlen(multi_ops[i]);

		if (lex_pos + (int)n <= lex_len &&
		    !strncmp(lex_src + lex_pos, multi_ops[i], n)) {
			lex_pos += (int)n;
			lex_col += (int)n;
			push_token(TK_PUNCT, xstrdup(multi_ops[i]), 0, line,
				   col);
			return true;
		}
	}
	if (strchr("+-*/%(){}[];,.<>=!~&|^?:", peekc())) {
		char *s = xmalloc(2);

		s[0] = (char)advancec();
		s[1] = '\0';
		push_token(TK_PUNCT, s, 0, line, col);
		return true;
	}
	return false;
}

static void lex_all(const char *src, int len)
{
	lex_src = src;
	lex_pos = 0;
	lex_len = len;

	for (;;) {
		int c = peekc();

		if (c < 0)
			break;
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
			advancec();
			continue;
		}
		if (c == '/' && peekc2() == '/') {
			while (peekc() >= 0 && peekc() != '\n')
				advancec();
			continue;
		}
		if (c == '/' && peekc2() == '*') {
			int line = lex_line;
			int col = lex_col;

			advancec();
			advancec();
			while (!(peekc() == '*' && peekc2() == '/')) {
				if (peekc() < 0)
					fatal("%d:%d: unterminated comment",
					      line, col);
				advancec();
			}
			advancec();
			advancec();
			continue;
		}
		if (c == '#')
			fatal("%d:%d: preprocessor directives are not supported",
			      lex_line, lex_col);
		if (starts_ident(c)) {
			lex_ident();
			continue;
		}
		if (c >= '0' && c <= '9') {
			lex_number();
			continue;
		}
		if (c == '\'') {
			lex_char();
			continue;
		}
		if (c == '"') {
			lex_string();
			continue;
		}
		if (lex_operator())
			continue;
		fatal("%d:%d: unexpected character '%c'", lex_line, lex_col, c);
	}
	push_token(TK_EOF, xstrdup(""), 0, lex_line, lex_col);
}

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

static void pp_add_inc_path(const char *path)
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

static void record_cli_macro(bool undef, const char *arg)
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

static void apply_cli_macros(void)
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

static int try_read_file(const char *path, char **out, int *out_len)
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

static char *preprocess(const char *path, const char *src, int len,
			int *out_len)
{
	struct buf out;

	buf_init(&out);
	pp_process(path, src, len, &out, NULL);
	*out_len = out.len;
	buf_byte(&out, 0);
	return (char *)out.data;
}

/* ------------------------------------------------------------------ */
/* types                                                               */
/* ------------------------------------------------------------------ */

enum {
	TY_VOID = 0,
	TY_CHAR,
	TY_SHORT,
	TY_INT,
	TY_LONG,
	TY_PTR,
	TY_ARRAY,
	TY_FUNC,
	TY_STRUCT,
	TY_UNION,
	TY_PLACEHOLDER,
};

struct obj;

struct param {
	char *name;
	struct type *ty;
	struct obj *obj; /* set only while generating a definition */
};

struct member {
	char *name;
	struct type *ty;
	int offset;
};

struct type {
	int kind;
	int size;
	int align;
	bool is_unsigned;
	bool is_complete;
	struct type *base;
	int array_len;
	struct param *params;
	int nparams;
	bool variadic;
	struct member *members;
	int nmembers;
	char *tag;
};

static struct type *ty_void;
static struct type *ty_char;
static struct type *ty_schar;
static struct type *ty_short;
static struct type *ty_ushort;
static struct type *ty_int;
static struct type *ty_uint;
static struct type *ty_long;
static struct type *ty_ulong;

static struct type *new_type(int kind, int size, int align, bool uns)
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

static struct type *ty_ptr(struct type *base)
{
	struct type *t = new_type(TY_PTR, 8, 8, true);

	t->base = base;
	return t;
}

static struct type *ty_array(struct type *base, int len)
{
	struct type *t = new_type(TY_ARRAY, 0, base->align, false);

	t->base = base;
	t->array_len = len;
	if (len >= 0)
		t->size = base->size * len;
	return t;
}

static struct type *ty_func(struct type *ret, struct param *params, int n,
			    bool variadic)
{
	struct type *t = new_type(TY_FUNC, 8, 8, false);

	t->base = ret;
	t->params = params;
	t->nparams = n;
	t->variadic = variadic;
	return t;
}

static struct type *new_aggregate_type(int kind, const char *tag)
{
	struct type *t = new_type(kind, 0, 1, false);

	t->is_complete = false;
	t->tag = tag ? xstrdup(tag) : NULL;
	return t;
}

static void init_types(void)
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

static bool is_integer(struct type *t)
{
	return t->kind == TY_CHAR || t->kind == TY_SHORT || t->kind == TY_INT ||
	       t->kind == TY_LONG;
}

static bool is_ptr_like(struct type *t)
{
	return t->kind == TY_PTR || t->kind == TY_ARRAY;
}

static bool is_aggregate(struct type *t)
{
	return t->kind == TY_STRUCT || t->kind == TY_UNION;
}

static bool is_scalar(struct type *t)
{
	return is_integer(t) || t->kind == TY_PTR;
}

static struct type *decay(struct type *t)
{
	if (t->kind == TY_ARRAY)
		return ty_ptr(t->base);
	if (t->kind == TY_FUNC)
		return ty_ptr(t);
	return t;
}

/* ------------------------------------------------------------------ */
/* AST                                                                 */
/* ------------------------------------------------------------------ */

enum {
	ND_NULL = 0,
	ND_MEMZERO,
	ND_NUM,
	ND_STR,
	ND_VAR,
	ND_ADD,
	ND_SUB,
	ND_MUL,
	ND_DIV,
	ND_MOD,
	ND_BITAND,
	ND_BITOR,
	ND_BITXOR,
	ND_SHL,
	ND_SHR,
	ND_EQ,
	ND_NE,
	ND_LT,
	ND_LE,
	ND_GT,
	ND_GE,
	ND_LOGAND,
	ND_LOGOR,
	ND_ASSIGN,
	ND_OPASSIGN,
	ND_PREINC,
	ND_POSTINC,
	ND_NEG,
	ND_NOT,
	ND_BITNOT,
	ND_ADDR,
	ND_DEREF,
	ND_MEMBER,
	ND_CALL,
	ND_CAST,
	ND_COND,
	ND_COMMA,
	ND_RETURN,
	ND_IF,
	ND_WHILE,
	ND_FOR,
	ND_DO,
	ND_BLOCK,
	ND_EXPR_STMT,
	ND_BREAK,
	ND_CONTINUE,
	ND_SWITCH,
	ND_CASE,
	ND_DEFAULT,
	ND_GOTO,
	ND_LABEL,
};

struct sym;
struct case_entry;

struct node {
	int kind;
	struct type *ty;
	struct node *lhs;
	struct node *rhs;
	struct node *cond;
	struct node *then;
	struct node *els;
	struct node *init;
	struct node *inc;
	struct node *body;
	struct node *next;
	struct node **args;
	int nargs;
	struct obj *var;
	struct node *callee;
	struct sym *sym;
	struct case_entry *cases;
	char *label_name;
	long val;
	int member_offset;
	int case_label;
};

static struct node *new_node(int kind)
{
	struct node *n = xcalloc(1, sizeof(*n));

	n->kind = kind;
	return n;
}

static struct node *new_num(long val)
{
	struct node *n = new_node(ND_NUM);

	n->val = val;
	n->ty = (val >= -2147483648L && val <= 2147483647L) ? ty_int : ty_long;
	return n;
}

static struct node *new_var_node(struct obj *var);

/* ------------------------------------------------------------------ */
/* symbols                                                             */
/* ------------------------------------------------------------------ */

enum {
	SEC_TEXT = 0,
	SEC_DATA,
	SEC_BSS,
};

struct data_reloc {
	int offset;
	struct sym *sym;
	long addend;
	struct data_reloc *next;
};

struct sym {
	char *name;
	int sec;
	int offset;
	int size;
	int align;
	bool defined;
	bool is_static;
	unsigned long addr;
	unsigned char *init;
	struct data_reloc *drel;
	struct sym *next;
};

struct obj {
	char *name;
	struct type *ty;
	bool is_local;
	bool is_function;
	bool defined;
	int offset;
	bool is_param;
	int param_index;
	struct sym *sym;
	struct node *body;
	struct obj *va_area;
	int frame_size;
	struct obj *next;
};

static struct obj *globals;
static struct sym *syms;
static struct sym *sym_tail;
static int unit_id;

static struct sym *new_sym(const char *name, int sec, int size, int align)
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

static void make_local_sym(struct sym *s)
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

static struct obj *find_global(const char *name)
{
	struct obj *o;

	for (o = globals; o; o = o->next) {
		if (!strcmp(o->name, name))
			return o;
	}
	return NULL;
}

static struct obj *new_global(const char *name, struct type *ty)
{
	struct obj *o = xcalloc(1, sizeof(*o));

	o->name = xstrdup(name);
	o->ty = ty;
	o->next = globals;
	globals = o;
	return o;
}

static struct obj *find_local(const char *name);

/* ------------------------------------------------------------------ */
/* scopes                                                              */
/* ------------------------------------------------------------------ */

struct scope {
	struct obj *vars;
	struct scope *parent;
};

static struct scope *scope;
static int locals_size;

static void enter_scope(void)
{
	struct scope *s = xcalloc(1, sizeof(*s));

	s->parent = scope;
	scope = s;
}

static void leave_scope(void)
{
	if (scope)
		scope = scope->parent;
}

static struct obj *find_local(const char *name)
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

static struct obj *new_local(const char *name, struct type *ty, bool is_param,
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

static struct node *new_var_node(struct obj *var)
{
	struct node *n = new_node(ND_VAR);

	n->var = var;
	n->ty = var->ty;
	return n;
}

/* ------------------------------------------------------------------ */
/* instruction encoding                                                */
/* ------------------------------------------------------------------ */

enum {
	X0 = 0,
	RA = 1,
	SP = 2,
	T0 = 5,
	T1 = 6,
	T2 = 7,
	S0 = 8,
	A0 = 10,
	A1 = 11,
	A2 = 12,
	T6 = 31,
};

static struct buf code;

static void emit32(unsigned int x)
{
	buf_u32(&code, x);
}

static unsigned int enc_r(int f7, int rs2, int rs1, int f3, int rd, int op)
{
	return ((unsigned int)f7 << 25) | ((unsigned int)rs2 << 20) |
	       ((unsigned int)rs1 << 15) | ((unsigned int)f3 << 12) |
	       ((unsigned int)rd << 7) | (unsigned int)op;
}

static unsigned int enc_i(int op, int rd, int f3, int rs1, int imm)
{
	return ((unsigned int)(imm & 0xfff) << 20) | ((unsigned int)rs1 << 15) |
	       ((unsigned int)f3 << 12) | ((unsigned int)rd << 7) |
	       (unsigned int)op;
}

static unsigned int enc_s(int op, int f3, int rs1, int rs2, int imm)
{
	unsigned int u = (unsigned int)(imm & 0xfff);

	return ((u >> 5) << 25) | ((unsigned int)rs2 << 20) |
	       ((unsigned int)rs1 << 15) | ((unsigned int)f3 << 12) |
	       ((u & 0x1f) << 7) | (unsigned int)op;
}

static unsigned int enc_b(int op, int f3, int rs1, int rs2, int imm)
{
	unsigned int u = (unsigned int)imm;

	return (((u >> 12) & 1) << 31) | (((u >> 5) & 0x3f) << 25) |
	       ((unsigned int)rs2 << 20) | ((unsigned int)rs1 << 15) |
	       ((unsigned int)f3 << 12) | (((u >> 1) & 0xf) << 8) |
	       (((u >> 11) & 1) << 7) | (unsigned int)op;
}

static unsigned int enc_u(int op, int rd, int imm20)
{
	return ((unsigned int)(imm20 & 0xfffff) << 12) |
	       ((unsigned int)rd << 7) | (unsigned int)op;
}

static unsigned int enc_j(int op, int rd, int imm)
{
	unsigned int u = (unsigned int)imm;

	return (((u >> 20) & 1) << 31) | (((u >> 1) & 0x3ff) << 21) |
	       (((u >> 11) & 1) << 20) | (((u >> 12) & 0xff) << 12) |
	       ((unsigned int)rd << 7) | (unsigned int)op;
}

static void i_addi(int rd, int rs, int imm)
{
	if (imm >= -2048 && imm <= 2047) {
		emit32(enc_i(0x13, rd, 0, rs, imm));
	} else {
		/* li T6, imm; add rd, rs, T6 */
		emit32(enc_i(0x13, T6, 0, X0, 0));
		{
			unsigned long u = (unsigned long)imm;
			int i;

			for (i = 7; i >= 0; i--) {
				emit32(enc_i(0x13, T6, 1, T6, 8)); /* slli */
				emit32(enc_i(0x13, T6, 6, T6,
					     (int)((u >> (i * 8)) & 0xff)));
			}
		}
		emit32(enc_r(0, T6, rs, 0, rd, 0x33)); /* add */
	}
}

static void i_addiw(int rd, int rs, int imm)
{
	emit32(enc_i(0x1b, rd, 0, rs, imm));
}

static void i_lui(int rd, int imm20)
{
	emit32(enc_u(0x37, rd, imm20));
}

static void i_ld(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 3, rs, imm));
}

static void i_sd(int rs2, int rs1, int imm)
{
	emit32(enc_s(0x23, 3, rs1, rs2, imm));
}

static void i_lw(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 2, rs, imm));
}

static void i_lwu(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 6, rs, imm));
}

static void i_sw(int rs2, int rs1, int imm)
{
	emit32(enc_s(0x23, 2, rs1, rs2, imm));
}

static void i_lh(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 1, rs, imm));
}

static void i_lhu(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 5, rs, imm));
}

static void i_sh(int rs2, int rs1, int imm)
{
	emit32(enc_s(0x23, 1, rs1, rs2, imm));
}

static void i_lb(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 0, rs, imm));
}

static void i_lbu(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 4, rs, imm));
}

static void i_sb(int rs2, int rs1, int imm)
{
	emit32(enc_s(0x23, 0, rs1, rs2, imm));
}

static void i_r(int f7, int f3, int rd, int rs1, int rs2)
{
	emit32(enc_r(f7, rs2, rs1, f3, rd, 0x33));
}

static void i_mv(int rd, int rs)
{
	i_addi(rd, rs, 0);
}

static void i_seqz(int rd, int rs)
{
	emit32(enc_i(0x13, rd, 3, rs, 1)); /* sltiu rd, rs, 1 */
}

static void i_snez(int rd, int rs)
{
	emit32(enc_r(0, rs, X0, 3, rd, 0x33)); /* sltu rd, x0, rs */
}

static void i_ret(void)
{
	emit32(enc_i(0x67, X0, 0, RA, 0)); /* jalr x0, 0(ra) */
}

static void i_ecall(void)
{
	emit32(0x00000073);
}

static void i_li(int rd, long val)
{
	unsigned long u = (unsigned long)val;

	if (val >= -2048 && val <= 2047) {
		emit32(enc_i(0x13, rd, 0, X0, (int)val));
		return;
	}
	if (val >= -2147483648L && val <= 2147483647L) {
		long hi = ((long)val + 0x800) >> 12;
		long lo = (long)val - (hi << 12);

		i_lui(rd, (int)hi);
		i_addiw(rd, rd, (int)lo);
		return;
	}
	emit32(enc_i(0x13, rd, 0, X0, 0));
	for (int i = 7; i >= 0; i--) {
		emit32(enc_i(0x13, rd, 1, rd, 8)); /* slli */
		emit32(enc_i(0x13, rd, 6, rd,
			     (int)((u >> (i * 8)) & 0xff))); /* ori */
	}
}

static void i_bf(int f3, int rs1, int rs2, int imm)
{
	emit32(enc_b(0x63, f3, rs1, rs2, imm));
}

/* ------------------------------------------------------------------ */
/* labels and relocations                                              */
/* ------------------------------------------------------------------ */

struct fixup {
	int pos;
	int label;
	struct fixup *next;
};

struct reloc {
	int pos;
	int kind;
	struct sym *sym;
	long addend;
	struct reloc *next;
};

enum {
	R_HI20 = 0,
	R_LO12I,
	R_JAL,
};

static struct fixup *fixups;
static struct reloc *relocs;
static int *label_pos;
static int nlabels;

struct unit {
	unsigned char *code;
	int code_len;
	struct sym *syms;
	struct reloc *relocs;
	unsigned char *data;
	int data_len;
	int bss_len;
	unsigned long text_base;
	unsigned long data_base;
	unsigned long bss_base;
	struct unit *next;
};

static struct unit *units;
static struct unit *unit_tail;

static int new_label(void)
{
	int id = nlabels++;

	label_pos = xrealloc(label_pos, (size_t)nlabels * sizeof(*label_pos));
	label_pos[id] = -1;
	return id;
}

static void place_label(int id)
{
	label_pos[id] = code.len;
}

static void add_fixup(int label)
{
	struct fixup *f = xcalloc(1, sizeof(*f));

	f->pos = code.len;
	f->label = label;
	f->next = fixups;
	fixups = f;
	emit32(enc_j(0x6f, X0, 0));
}

static void jump_if_zero(int reg, int label)
{
	i_bf(1, reg, X0, 8); /* bne reg, x0, +8 */
	add_fixup(label);
}

static void jump_if_nonzero(int reg, int label)
{
	i_bf(0, reg, X0, 8); /* beq reg, x0, +8 */
	add_fixup(label);
}

static void jump_to(int label)
{
	add_fixup(label);
}

static void patch_fixups(void)
{
	struct fixup *f;

	for (f = fixups; f; f = f->next) {
		int off;

		if (label_pos[f->label] < 0)
			fatal("internal error: unresolved label");
		off = label_pos[f->label] - f->pos;
		if (off < -(1 << 20) || off >= (1 << 20))
			fatal("function too large for jump");
		code.data[f->pos + 0] =
			(unsigned char)(enc_j(0x6f, X0, off) & 0xff);
		code.data[f->pos + 1] =
			(unsigned char)((enc_j(0x6f, X0, off) >> 8) & 0xff);
		code.data[f->pos + 2] =
			(unsigned char)((enc_j(0x6f, X0, off) >> 16) & 0xff);
		code.data[f->pos + 3] =
			(unsigned char)((enc_j(0x6f, X0, off) >> 24) & 0xff);
	}
	fixups = NULL;
}

static void add_reloc(int kind, struct sym *sym, long addend)
{
	struct reloc *r = xcalloc(1, sizeof(*r));

	r->pos = code.len;
	r->kind = kind;
	r->sym = sym;
	r->addend = addend;
	r->next = relocs;
	relocs = r;
}

static void i_la(int rd, struct sym *sym, long addend)
{
	add_reloc(R_HI20, sym, addend);
	emit32(enc_u(0x37, rd, 0));
	add_reloc(R_LO12I, sym, addend);
	emit32(enc_i(0x13, rd, 0, rd, 0));
}

static void i_call_sym(struct sym *sym)
{
	add_reloc(R_JAL, sym, 0);
	emit32(enc_j(0x6f, RA, 0));
}

static unsigned int rd_of(unsigned int ins)
{
	return (ins >> 7) & 0x1f;
}

static unsigned int rs1_of(unsigned int ins)
{
	return (ins >> 15) & 0x1f;
}

/* ------------------------------------------------------------------ */
/* parser state                                                        */
/* ------------------------------------------------------------------ */

static int tok_pos;

struct case_entry {
	struct node *node;
	long val;
	bool is_default;
	struct case_entry *next;
};

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
		i_li(17, 1); /* a7 = SYS_write */
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
		i_li(17, 60); /* a7 = SYS_exit */
		i_ecall();
		emit32(enc_j(0x6f, X0, 0));
		return;
	}

	if (builtin_ok && (!strcmp(name, "write") || !strcmp(name, "read") ||
			   !strcmp(name, "open") || !strcmp(name, "close"))) {
		int sysno;

		if (!strcmp(name, "write"))
			sysno = 1;
		else if (!strcmp(name, "read"))
			sysno = 0;
		else if (!strcmp(name, "open"))
			sysno = 2;
		else
			sysno = 3;
		if (!strcmp(name, "open") && n->nargs < 3)
			i_li(A2, 0);
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

static void gen_function(struct obj *fn)
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

static void gen_runtime(void)
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
	i_li(17, 1);
	i_ecall();
	/* write(1, "\n", 1) */
	i_addi(SP, SP, -16);
	i_li(T0, '\n');
	i_sb(T0, SP, 0);
	i_li(A0, 1);
	i_mv(A1, SP);
	i_li(A2, 1);
	i_li(17, 1);
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

static void gen_start(struct sym *main_sym)
{
	static struct sym *start_sym;

	start_sym = new_sym("__cc_start", SEC_TEXT, 0, 4);
	start_sym->defined = true;
	start_sym->offset = code.len;
	i_call_sym(main_sym);
	i_li(17, 60);
	i_ecall();
	emit32(enc_j(0x6f, X0, 0));
}

/* ------------------------------------------------------------------ */
/* ELF writer                                                          */
/* ------------------------------------------------------------------ */

#define USER_BASE 0x10000UL
#define TEXT_FILE_OFF 0x1000
#define EM_RISCV 243

static void put16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
}

static void put32(unsigned char *p, unsigned long v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

static void put64(unsigned char *p, unsigned long v)
{
	int i;

	for (i = 0; i < 8; i++)
		p[i] = (unsigned char)((v >> (i * 8)) & 0xff);
}

struct link_sym {
	char *name;
	struct sym *sym;
	struct link_sym *next;
};

static struct link_sym *link_syms;

static struct link_sym *link_find(const char *name)
{
	struct link_sym *l;

	for (l = link_syms; l; l = l->next) {
		if (!strcmp(l->name, name))
			return l;
	}
	return NULL;
}

static void link_define(struct sym *s)
{
	struct link_sym *l;

	if (!s->defined || s->is_static)
		return;
	if (link_find(s->name))
		fatal("duplicate symbol '%s'", s->name);
	l = xcalloc(1, sizeof(*l));
	l->name = xstrdup(s->name);
	l->sym = s;
	l->next = link_syms;
	link_syms = l;
}

static unsigned long resolve_sym(struct sym *s)
{
	struct link_sym *l;

	if (s->defined)
		return s->addr;
	l = link_find(s->name);
	if (!l)
		fatal("undefined symbol '%s'", s->name);
	return l->sym->addr;
}

static void put_ins(unsigned char *buf, int pos, unsigned int ins)
{
	buf[pos + 0] = (unsigned char)(ins & 0xff);
	buf[pos + 1] = (unsigned char)((ins >> 8) & 0xff);
	buf[pos + 2] = (unsigned char)((ins >> 16) & 0xff);
	buf[pos + 3] = (unsigned char)((ins >> 24) & 0xff);
}

static void finalize_unit(void)
{
	struct unit *u = xcalloc(1, sizeof(*u));
	struct sym *s;
	struct buf data;
	int off;

	u->code_len = code.len;
	u->code = xmalloc((size_t)(code.len > 0 ? code.len : 1));
	if (code.len > 0)
		memcpy(u->code, code.data, (size_t)code.len);
	u->syms = syms;
	u->relocs = relocs;

	off = 0;
	for (s = syms; s; s = s->next) {
		if (!s->defined || s->sec != SEC_DATA)
			continue;
		off = align_to(off, s->align);
		s->offset = off;
		off += s->size;
	}

	buf_init(&data);
	for (s = syms; s; s = s->next) {
		if (!s->defined || s->sec != SEC_DATA)
			continue;
		buf_align(&data, s->align);
		if (s->init && s->size > 0) {
			buf_grow(&data, s->size);
			memcpy(data.data + data.len, s->init, (size_t)s->size);
			data.len += s->size;
		} else {
			buf_pad(&data, s->size);
		}
	}
	u->data = data.data;
	u->data_len = data.len;

	off = 0;
	for (s = syms; s; s = s->next) {
		if (!s->defined || s->sec != SEC_BSS)
			continue;
		off = align_to(off, s->align);
		s->offset = off;
		off += s->size;
	}
	u->bss_len = off;

	if (unit_tail)
		unit_tail->next = u;
	else
		units = u;
	unit_tail = u;
}

static void reset_unit_state(void)
{
	code.len = 0;
	syms = NULL;
	sym_tail = NULL;
	relocs = NULL;
	fixups = NULL;
	nlabels = 0;
	label_pos = NULL;
	globals = NULL;
	typedefs = NULL;
	tags = NULL;
	consts = NULL;
	scope = NULL;
	locals_size = 0;
	puts_sym = NULL;
	memcpy_sym = NULL;
	memset_sym = NULL;
	cur_break = -1;
	cur_cont = -1;
	tok_pos = 0;
	ntokens = 0;
	pp_include_depth = 0;
}

static struct sym *new_runtime_ref(const char *name)
{
	struct sym *s = new_sym(name, SEC_TEXT, 0, 4);

	s->defined = false;
	return s;
}

static void make_runtime_refs(void)
{
	puts_sym = new_runtime_ref("__cc_puts");
	memcpy_sym = new_runtime_ref("__cc_memcpy");
	memset_sym = new_runtime_ref("__cc_memset");
}

static void patch_unit_relocs(struct unit *u, unsigned long text_section_base)
{
	struct reloc *r;
	struct sym *s;

	for (r = u->relocs; r; r = r->next) {
		unsigned long target =
			resolve_sym(r->sym) + (unsigned long)r->addend;
		unsigned long pos_abs = text_section_base + u->text_base +
					(unsigned long)r->pos;
		unsigned int old;
		unsigned int ins;

		memcpy(&old, u->code + r->pos, 4);
		switch (r->kind) {
		case R_HI20: {
			long hi = (long)((target + 0x800) >> 12);

			ins = enc_u(0x37, (int)rd_of(old), (int)hi);
			break;
		}
		case R_LO12I: {
			long lo = (long)(target & 0xfff);

			if (lo & 0x800)
				lo -= 0x1000;
			ins = enc_i(0x13, (int)rd_of(old), 0, (int)rs1_of(old),
				    (int)lo);
			break;
		}
		case R_JAL: {
			long off = (long)target - (long)pos_abs;

			if (off & 1)
				fatal("internal error: unaligned call");
			if (off < -(1 << 20) || off >= (1 << 20))
				fatal("program too large for direct call");
			ins = enc_j(0x6f, (int)rd_of(old), (int)off);
			break;
		}
		default:
			fatal("internal error: bad relocation");
			return;
		}
		put_ins(u->code, r->pos, ins);
	}

	for (s = u->syms; s; s = s->next) {
		struct data_reloc *dr;

		if (!s->defined || s->sec != SEC_DATA)
			continue;
		for (dr = s->drel; dr; dr = dr->next) {
			unsigned long target = resolve_sym(dr->sym) +
					       (unsigned long)dr->addend;
			int at = s->offset + dr->offset;

			if (at + 8 > u->data_len)
				fatal("internal error: data relocation overflow");
			put64(u->data + at, target);
		}
	}
}

static struct sym *find_main_sym(void)
{
	struct sym *found = NULL;
	struct unit *u;

	for (u = units; u; u = u->next) {
		struct sym *s;

		for (s = u->syms; s; s = s->next) {
			if (!s->defined || s->is_static || s->sec != SEC_TEXT)
				continue;
			if (!strcmp(s->name, "main")) {
				if (found)
					fatal("multiple definitions of 'main'");
				found = s;
			}
		}
	}
	return found;
}

static void ensure_runtime_unit(void)
{
	reset_unit_state();
	gen_runtime();
	finalize_unit();
}

static void ensure_start_unit(void)
{
	struct sym *main_sym = find_main_sym();

	if (!main_sym)
		fatal("no definition of 'main'");
	reset_unit_state();
	gen_start(main_sym);
	finalize_unit();
}

static void link_and_write(const char *path)
{
	struct unit *u;
	struct sym *s;
	struct link_sym *l;
	unsigned long text_off = 0;
	unsigned long data_off = 0;
	unsigned long bss_off = 0;
	unsigned long text_section_base;
	unsigned long data_section_base;
	unsigned long bss_section_base;
	unsigned long file_len;
	unsigned long mem_len;
	unsigned long entry;
	unsigned char *text;
	unsigned char *data;
	unsigned char *file;
	int data_file_off;
	int fd;
	int written;

	ensure_runtime_unit();
	ensure_start_unit();

	link_syms = NULL;
	for (u = units; u; u = u->next) {
		text_off = (unsigned long)align_to((int)text_off, 16);
		u->text_base = text_off;
		text_off += (unsigned long)u->code_len;
	}
	for (u = units; u; u = u->next) {
		data_off = (unsigned long)align_to((int)data_off, 16);
		u->data_base = data_off;
		data_off += (unsigned long)u->data_len;
	}
	for (u = units; u; u = u->next) {
		bss_off = (unsigned long)align_to((int)bss_off, 16);
		u->bss_base = bss_off;
		bss_off += (unsigned long)u->bss_len;
	}

	text_section_base = USER_BASE + TEXT_FILE_OFF;
	data_section_base = (unsigned long)align_to(
		(int)(text_section_base + text_off), 16);
	bss_section_base = (unsigned long)align_to(
		(int)(data_section_base + data_off), 16);

	for (u = units; u; u = u->next) {
		for (s = u->syms; s; s = s->next)
			link_define(s);
	}
	for (u = units; u; u = u->next) {
		for (s = u->syms; s; s = s->next) {
			if (!s->defined)
				continue;
			if (s->sec == SEC_TEXT)
				s->addr = text_section_base + u->text_base +
					  (unsigned long)s->offset;
			else if (s->sec == SEC_DATA)
				s->addr = data_section_base + u->data_base +
					  (unsigned long)s->offset;
			else
				s->addr = bss_section_base + u->bss_base +
					  (unsigned long)s->offset;
		}
	}
	for (u = units; u; u = u->next)
		patch_unit_relocs(u, text_section_base);

	l = link_find("__cc_start");
	if (!l)
		fatal("no definition of 'main'");
	entry = l->sym->addr;

	text = xcalloc(1, text_off ? (size_t)text_off : 1);
	for (u = units; u; u = u->next) {
		if (u->code_len)
			memcpy(text + u->text_base, u->code,
			       (size_t)u->code_len);
	}
	data = xcalloc(1, data_off ? (size_t)data_off : 1);
	for (u = units; u; u = u->next) {
		if (u->data_len)
			memcpy(data + u->data_base, u->data,
			       (size_t)u->data_len);
	}

	data_file_off = (int)(data_section_base - USER_BASE);
	file_len = (unsigned long)data_file_off + data_off;
	mem_len = (bss_section_base - USER_BASE) + bss_off;
	file = xcalloc(1, (size_t)file_len);
	memcpy(file + TEXT_FILE_OFF, text, (size_t)text_off);
	if (data_off)
		memcpy(file + data_file_off, data, (size_t)data_off);

	/* ELF header */
	file[0] = 0x7f;
	file[1] = 'E';
	file[2] = 'L';
	file[3] = 'F';
	file[4] = 2; /* ELFCLASS64 */
	file[5] = 1; /* little endian */
	file[6] = 1; /* version */
	put16(file + 16, 2); /* ET_EXEC */
	put16(file + 18, EM_RISCV); /* e_machine */
	put32(file + 20, 1); /* e_version */
	put64(file + 24, entry);
	put64(file + 32, 64); /* e_phoff */
	put64(file + 40, 0); /* e_shoff */
	put32(file + 48, 0); /* e_flags */
	put16(file + 52, 64); /* e_ehsize */
	put16(file + 54, 56); /* e_phentsize */
	put16(file + 56, 1); /* e_phnum */
	put16(file + 58, 0); /* e_shentsize */
	put16(file + 60, 0); /* e_shnum */
	put16(file + 62, 0); /* e_shstrndx */

	/* program header: one RWX PT_LOAD */
	put32(file + 64, 1); /* PT_LOAD */
	put32(file + 68, 7); /* R|W|X */
	put64(file + 72, 0); /* p_offset */
	put64(file + 80, USER_BASE);
	put64(file + 88, USER_BASE);
	put64(file + 96, file_len);
	put64(file + 104, mem_len > file_len ? mem_len : file_len);
	put64(file + 112, 0x1000);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (fd < 0)
		fatal("cannot open output file '%s'", path);
	written = 0;
	while (written < (int)file_len) {
		ssize_t n = write(fd, file + written,
				  (size_t)((int)file_len - written));

		if (n <= 0)
			fatal("write failed");
		written += (int)n;
	}
	close(fd);
	chmod(path, 0755);
	free(file);
	free(text);
	free(data);
}

struct rd {
	const unsigned char *p;
	int len;
	int pos;
};

static unsigned char rd_u8(struct rd *r)
{
	if (r->pos + 1 > r->len)
		fatal("truncated object file");
	return r->p[r->pos++];
}

static unsigned int rd_u32(struct rd *r)
{
	unsigned int v;

	if (r->pos + 4 > r->len)
		fatal("truncated object file");
	v = (unsigned int)r->p[r->pos] | ((unsigned int)r->p[r->pos + 1] << 8) |
	    ((unsigned int)r->p[r->pos + 2] << 16) |
	    ((unsigned int)r->p[r->pos + 3] << 24);
	r->pos += 4;
	return v;
}

static unsigned long rd_u64(struct rd *r)
{
	unsigned long v = 0;
	int i;

	if (r->pos + 8 > r->len)
		fatal("truncated object file");
	for (i = 0; i < 8; i++)
		v |= (unsigned long)r->p[r->pos + i] << (i * 8);
	r->pos += 8;
	return v;
}

static void rd_bytes(struct rd *r, void *dst, int n)
{
	if (n < 0 || r->pos + n > r->len)
		fatal("truncated object file");
	memcpy(dst, r->p + r->pos, (size_t)n);
	r->pos += n;
}

static char *rd_name(struct rd *r)
{
	unsigned int n = rd_u32(r);
	char *s;

	if ((int)n < 0 || r->pos + (int)n > r->len)
		fatal("truncated object file");
	s = xmalloc((size_t)n + 1);
	memcpy(s, r->p + r->pos, n);
	s[n] = '\0';
	r->pos += (int)n;
	return s;
}

static void write_file_bytes(const char *path, const unsigned char *buf,
			     int len, int mode)
{
	int fd;
	int off = 0;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0)
		fatal("cannot open '%s'", path);
	while (off < len) {
		ssize_t n = write(fd, buf + off, (size_t)(len - off));

		if (n <= 0)
			fatal("write failed");
		off += (int)n;
	}
	close(fd);
	chmod(path, mode);
}

static int sym_index(struct sym **arr, int n, struct sym *s)
{
	int i;

	for (i = 0; i < n; i++) {
		if (arr[i] == s)
			return i;
	}
	fatal("internal error: object relocation target is not in symbol table");
	return 0;
}

static void write_object(const char *path, struct unit *u)
{
	struct buf b;
	struct sym **arr;
	struct sym *s;
	struct reloc *r;
	int nsyms = 0;
	int ndrels = 0;
	int nrelocs = 0;
	int i = 0;

	for (s = u->syms; s; s = s->next)
		nsyms++;
	for (s = u->syms; s; s = s->next) {
		struct data_reloc *dr;

		for (dr = s->drel; dr; dr = dr->next)
			ndrels++;
	}
	for (r = u->relocs; r; r = r->next)
		nrelocs++;

	arr = xcalloc((size_t)(nsyms > 0 ? nsyms : 1), sizeof(*arr));
	for (s = u->syms; s; s = s->next)
		arr[i++] = s;

	buf_init(&b);
	buf_byte(&b, 'C');
	buf_byte(&b, 'C');
	buf_byte(&b, 'O');
	buf_byte(&b, '1');
	buf_u32(&b, 1); /* version */
	buf_u32(&b, (unsigned int)u->code_len);
	buf_u32(&b, (unsigned int)u->data_len);
	buf_u32(&b, (unsigned int)u->bss_len);
	buf_u32(&b, (unsigned int)nsyms);
	buf_u32(&b, (unsigned int)ndrels);
	buf_u32(&b, (unsigned int)nrelocs);

	buf_grow(&b, u->code_len);
	memcpy(b.data + b.len, u->code, (size_t)u->code_len);
	b.len += u->code_len;
	buf_grow(&b, u->data_len);
	memcpy(b.data + b.len, u->data, (size_t)u->data_len);
	b.len += u->data_len;

	for (i = 0; i < nsyms; i++) {
		int n = (int)strlen(arr[i]->name);

		buf_u32(&b, (unsigned int)n);
		buf_grow(&b, n);
		memcpy(b.data + b.len, arr[i]->name, (size_t)n);
		b.len += n;
		buf_u32(&b, (unsigned int)arr[i]->sec);
		buf_u32(&b, (unsigned int)arr[i]->offset);
		buf_u32(&b, (unsigned int)arr[i]->size);
		buf_u32(&b, (unsigned int)arr[i]->align);
		buf_byte(&b, arr[i]->defined ? 1 : 0);
		buf_byte(&b, arr[i]->is_static ? 1 : 0);
	}

	for (i = 0; i < nsyms; i++) {
		struct data_reloc *dr;

		for (dr = arr[i]->drel; dr; dr = dr->next) {
			buf_u32(&b, (unsigned int)i);
			buf_u32(&b, (unsigned int)dr->offset);
			buf_u32(&b,
				(unsigned int)sym_index(arr, nsyms, dr->sym));
			buf_u64(&b, (unsigned long)dr->addend);
		}
	}
	for (r = u->relocs; r; r = r->next) {
		buf_u32(&b, (unsigned int)r->pos);
		buf_u32(&b, (unsigned int)r->kind);
		buf_u32(&b, (unsigned int)sym_index(arr, nsyms, r->sym));
		buf_u64(&b, (unsigned long)r->addend);
	}

	write_file_bytes(path, b.data, b.len, 0644);
	free(arr);
}

static struct unit *read_object(const char *path)
{
	struct unit *u;
	struct sym **arr;
	struct sym *tail = NULL;
	char *buf;
	int len;
	struct rd rd;
	unsigned int nsyms;
	unsigned int ndrels;
	unsigned int nrelocs;
	int i;

	if (try_read_file(path, &buf, &len) < 0)
		fatal("cannot open object file '%s'", path);
	memset(&rd, 0, sizeof(rd));
	rd.p = (const unsigned char *)buf;
	rd.len = len;
	if (rd_u8(&rd) != 'C' || rd_u8(&rd) != 'C' || rd_u8(&rd) != 'O' ||
	    rd_u8(&rd) != '1')
		fatal("'%s' is not a cc object file", path);
	if (rd_u32(&rd) != 1)
		fatal("unsupported object file version in '%s'", path);

	u = xcalloc(1, sizeof(*u));
	u->code_len = (int)rd_u32(&rd);
	u->data_len = (int)rd_u32(&rd);
	u->bss_len = (int)rd_u32(&rd);
	nsyms = rd_u32(&rd);
	ndrels = rd_u32(&rd);
	nrelocs = rd_u32(&rd);

	u->code = xmalloc((size_t)(u->code_len > 0 ? u->code_len : 1));
	rd_bytes(&rd, u->code, u->code_len);
	u->data = xmalloc((size_t)(u->data_len > 0 ? u->data_len : 1));
	rd_bytes(&rd, u->data, u->data_len);

	arr = xcalloc((size_t)(nsyms > 0 ? nsyms : 1), sizeof(*arr));
	for (i = 0; i < (int)nsyms; i++) {
		struct sym *s = xcalloc(1, sizeof(*s));

		s->name = rd_name(&rd);
		s->sec = (int)rd_u32(&rd);
		s->offset = (int)rd_u32(&rd);
		s->size = (int)rd_u32(&rd);
		s->align = (int)rd_u32(&rd);
		s->defined = rd_u8(&rd) != 0;
		s->is_static = rd_u8(&rd) != 0;
		arr[i] = s;
		if (tail)
			tail->next = s;
		else
			u->syms = s;
		tail = s;
	}

	for (i = 0; i < (int)ndrels; i++) {
		unsigned int owner = rd_u32(&rd);
		unsigned int off = rd_u32(&rd);
		unsigned int target = rd_u32(&rd);
		unsigned long addend = rd_u64(&rd);
		struct data_reloc *dr;

		if (owner >= nsyms || target >= nsyms)
			fatal("bad symbol index in object file '%s'", path);
		dr = xcalloc(1, sizeof(*dr));
		dr->offset = (int)off;
		dr->sym = arr[target];
		dr->addend = (long)addend;
		dr->next = arr[owner]->drel;
		arr[owner]->drel = dr;
	}
	for (i = 0; i < (int)nrelocs; i++) {
		unsigned int pos = rd_u32(&rd);
		unsigned int kind = rd_u32(&rd);
		unsigned int target = rd_u32(&rd);
		unsigned long addend = rd_u64(&rd);
		struct reloc *r;

		if (target >= nsyms)
			fatal("bad symbol index in object file '%s'", path);
		r = xcalloc(1, sizeof(*r));
		r->pos = (int)pos;
		r->kind = (int)kind;
		r->sym = arr[target];
		r->addend = (long)addend;
		r->next = u->relocs;
		u->relocs = r;
	}
	if (rd.pos != rd.len)
		fatal("trailing data in object file '%s'", path);

	free(arr);
	free(buf);
	if (unit_tail)
		unit_tail->next = u;
	else
		units = u;
	unit_tail = u;
	return u;
}

/* ------------------------------------------------------------------ */
/* top level                                                           */
/* ------------------------------------------------------------------ */

static char *read_source(const char *path, int *out_len)
{
	char *buf;

	if (try_read_file(path, &buf, out_len) < 0)
		fatal("cannot open '%s'", path);
	return buf;
}

static void parse_translation_unit(void)
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

static void compile_source(const char *path)
{
	char *src;
	char *pp;
	int len;
	int pplen;

	reset_unit_state();
	apply_cli_macros();

	src = read_source(path, &len);
	pp = preprocess(path, src, len, &pplen);
	free(src);
	lex_all(pp, pplen);
	free(pp);
	parse_translation_unit();

	/* Runtime is injected once by the linker; this unit only references it. */
	make_runtime_refs();

	for (struct obj *o = globals; o; o = o->next) {
		if (!o->is_function || !o->defined)
			continue;
		gen_function(o);
	}

	finalize_unit();
	unit_id++;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [-c] [-E] [-o output] [-I dir] [-D name[=value]] "
		"[-U name] input.c|input.o [...]\n",
		argv0);
}

static bool ends_with(const char *s, const char *suffix)
{
	size_t n = strlen(s);
	size_t m = strlen(suffix);

	return n >= m && !strcmp(s + n - m, suffix);
}

static char *object_path(const char *input)
{
	const char *base = strrchr(input, '/');
	size_t n;
	char *p;

	base = base ? base + 1 : input;
	n = strlen(base);
	p = xmalloc(n + 3);
	if (n > 2 && !strcmp(base + n - 2, ".c")) {
		memcpy(p, base, n - 2);
		p[n - 2] = '\0';
	} else {
		memcpy(p, base, n + 1);
	}
	strcat(p, ".o");
	return p;
}

static void write_stdout(const char *buf, int len)
{
	int off = 0;

	while (off < len) {
		ssize_t n = write(1, buf + off, (size_t)(len - off));

		if (n <= 0)
			fatal("write failed");
		off += (int)n;
	}
}

int main(int argc, char **argv)
{
	const char *out = "a.out";
	const char **inputs = NULL;
	int ninputs = 0;
	int i;
	bool preprocess_only = false;
	bool compile_only = false;
	bool out_given = false;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-o")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			out = argv[++i];
			out_given = true;
		} else if (!strcmp(argv[i], "-c")) {
			compile_only = true;
		} else if (!strcmp(argv[i], "-I")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			pp_add_inc_path(argv[++i]);
		} else if (!strncmp(argv[i], "-I", 2) && argv[i][2]) {
			pp_add_inc_path(argv[i] + 2);
		} else if (!strcmp(argv[i], "-D")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			record_cli_macro(false, argv[++i]);
		} else if (!strncmp(argv[i], "-D", 2) && argv[i][2]) {
			record_cli_macro(false, argv[i] + 2);
		} else if (!strcmp(argv[i], "-U")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			record_cli_macro(true, argv[++i]);
		} else if (!strncmp(argv[i], "-U", 2) && argv[i][2]) {
			record_cli_macro(true, argv[i] + 2);
		} else if (!strcmp(argv[i], "-E")) {
			preprocess_only = true;
		} else if (!strcmp(argv[i], "-h") ||
			   !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return 0;
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "cc: unknown option '%s'\n", argv[i]);
			usage(argv[0]);
			return 2;
		} else {
			inputs = xrealloc(inputs, (size_t)(ninputs + 1) *
							  sizeof(*inputs));
			inputs[ninputs++] = argv[i];
		}
	}
	if (ninputs == 0) {
		usage(argv[0]);
		return 2;
	}
	if (compile_only && preprocess_only)
		fatal("-c and -E are mutually exclusive");
	if (compile_only && ninputs > 1 && out_given)
		fatal("-o with -c requires a single input file");

	pp_add_inc_path(".");
	pp_add_inc_path("/usr/include");
	pp_add_inc_path("/usr/local/include");

	init_types();
	buf_init(&code);

	if (preprocess_only) {
		for (i = 0; i < ninputs; i++) {
			char *src;
			char *pp;
			int len;
			int pplen;

			apply_cli_macros();
			src = read_source(inputs[i], &len);
			pp = preprocess(inputs[i], src, len, &pplen);
			free(src);
			write_stdout(pp, pplen);
			free(pp);
		}
		return 0;
	}

	if (compile_only) {
		for (i = 0; i < ninputs; i++) {
			char *obj;

			if (ends_with(inputs[i], ".o"))
				fatal("cannot use -c with object file '%s'",
				      inputs[i]);
			compile_source(inputs[i]);
			obj = (out_given && ninputs == 1) ?
				      (char *)out :
				      object_path(inputs[i]);
			write_object(obj, unit_tail);
			if (obj != out)
				free(obj);
		}
		return 0;
	}

	for (i = 0; i < ninputs; i++) {
		if (ends_with(inputs[i], ".o"))
			(void)read_object(inputs[i]);
		else
			compile_source(inputs[i]);
	}

	link_and_write(out);
	return 0;
}
