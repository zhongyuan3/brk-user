#include "cc.h"

struct token *tokens;
int ntokens;
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

bool starts_ident(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_ident_char(int c)
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

void lex_all(const char *src, int len)
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

void reset_lexer_state(void)
{
	ntokens = 0;
}
