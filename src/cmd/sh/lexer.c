#include "sh.h"

void sh_lexer_init(struct sh_lexer *lex, const char *input, size_t len)
{
	lex->input = input;
	lex->pos = 0;
	lex->len = len;
	lex->line = 1;
	lex->col = 1;
	lex->errmsg[0] = '\0';
}

void sh_token_release(struct sh_token *tok)
{
	if (tok && tok->type == SH_TOKEN_WORD && tok->text) {
		free(tok->text);
		tok->text = NULL;
	}
}

static int at_eof(const struct sh_lexer *lex)
{
	return lex->pos >= lex->len;
}

static char peek(const struct sh_lexer *lex)
{
	return at_eof(lex) ? '\0' : lex->input[lex->pos];
}

static void advance(struct sh_lexer *lex)
{
	if (at_eof(lex))
		return;
	if (lex->input[lex->pos] == '\n') {
		lex->line++;
		lex->col = 1;
	} else {
		lex->col++;
	}
	lex->pos++;
}

static void skip_spaces(struct sh_lexer *lex)
{
	while (!at_eof(lex) && isspace((unsigned char)peek(lex)) &&
	       peek(lex) != '\n')
		advance(lex);
}

static void skip_comment(struct sh_lexer *lex)
{
	if (peek(lex) != '#')
		return;
	while (!at_eof(lex) && peek(lex) != '\n')
		advance(lex);
}

char *sh_buf_push(char *buf, size_t *len, size_t *cap, char c)
{
	if (*len + 1 >= *cap) {
		size_t ncap = *cap ? *cap * 2 : 64;
		char *nb = realloc(buf, ncap);

		if (!nb)
			return NULL;
		buf = nb;
		*cap = ncap;
	}
	buf[(*len)++] = c;
	return buf;
}

static int append_value(char **buf, size_t *len, size_t *cap, const char *value)
{
	char *b = *buf;

	if (!value)
		return 0;

	for (const char *p = value; *p; p++) {
		b = sh_buf_push(b, len, cap, *p);
		if (!b)
			return -1;
		*buf = b;
	}
	return 0;
}

static int append_long(char **buf, size_t *len, size_t *cap, long value)
{
	char text[32];
	int n = snprintf(text, sizeof(text), "%ld", value);

	if (n < 0 || (size_t)n >= sizeof(text))
		return -1;
	return append_value(buf, len, cap, text);
}

static int append_parameters(char **buf, size_t *len, size_t *cap)
{
	for (int i = 0; i < sh_argc; i++) {
		if (i > 0 && append_value(buf, len, cap, " ") != 0)
			return -1;
		if (append_value(buf, len, cap, sh_argv[i]) != 0)
			return -1;
	}
	return 0;
}

/*
 * Expand $NAME, ${NAME}, special parameters and positional parameters.
 * Returns 0 on success, including the case where the variable is unset or
 * empty.  *buf may remain NULL when nothing was appended.  Returns -1 on
 * allocation failure.
 */
static int expand_variable(struct sh_lexer *lex, char **buf, size_t *len,
			   size_t *cap, int *consumed)
{
	char name[128];
	size_t name_len = 0;
	size_t save = lex->pos;
	int braced = 0;
	int special = 0;
	int index = -1;

	*consumed = 0;
	advance(lex); /* $ */

	if (peek(lex) == '{') {
		braced = 1;
		advance(lex);
	}

	if (braced) {
		char c = peek(lex);

		if (c && strchr("?$#@*-!", c)) {
			special = c;
			advance(lex);
			if (peek(lex) != '}') {
				lex->pos = save;
				return 0;
			}
			advance(lex);
			goto expand;
		}
		if (isdigit((unsigned char)c)) {
			index = 0;
			while (isdigit((unsigned char)peek(lex))) {
				if (index < 100000)
					index = index * 10 + (peek(lex) - '0');
				advance(lex);
			}
			if (peek(lex) != '}') {
				lex->pos = save;
				return 0;
			}
			advance(lex);
			goto expand;
		}
		if (!isalpha((unsigned char)c) && c != '_') {
			lex->pos = save;
			return 0;
		}
	} else {
		char c = peek(lex);

		if (isdigit((unsigned char)c)) {
			index = c - '0';
			advance(lex);
			goto expand;
		}
		if (c && strchr("?$#@*-!", c)) {
			special = c;
			advance(lex);
			goto expand;
		}
		if (!isalpha((unsigned char)c) && c != '_') {
			lex->pos = save;
			return 0;
		}
	}

	while (!at_eof(lex) &&
	       (isalnum((unsigned char)peek(lex)) || peek(lex) == '_')) {
		if (name_len + 1 < sizeof(name))
			name[name_len] = peek(lex);
		name_len++;
		advance(lex);
	}

	if (braced) {
		if (peek(lex) != '}' || name_len == 0) {
			lex->pos = save;
			return 0;
		}
		advance(lex);
	}

	if (name_len == 0 || name_len >= sizeof(name)) {
		lex->pos = save;
		return 0;
	}
	name[name_len] = '\0';

	if (append_value(buf, len, cap, getenv(name)) != 0)
		return -1;
	*consumed = 1;
	return 0;

expand:
	if (index >= 0) {
		const char *value = NULL;

		if (index == 0)
			value = sh_arg0;
		else if (index <= sh_argc && sh_argv)
			value = sh_argv[index - 1];
		if (append_value(buf, len, cap, value) != 0)
			return -1;
	} else {
		switch (special) {
		case '?':
			if (append_long(buf, len, cap, sh_last_status) != 0)
				return -1;
			break;
		case '$':
			if (append_long(buf, len, cap, (long)getpid()) != 0)
				return -1;
			break;
		case '#':
			if (append_long(buf, len, cap, sh_argc) != 0)
				return -1;
			break;
		case '@':
		case '*':
			if (append_parameters(buf, len, cap) != 0)
				return -1;
			break;
		case '-':
		case '!':
			break;
		default:
			break;
		}
	}

	*consumed = 1;
	return 0;
}

static int read_single_quoted(struct sh_lexer *lex, char **out)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;

	advance(lex); /* opening ' */
	while (!at_eof(lex) && peek(lex) != '\'') {
		buf = sh_buf_push(buf, &len, &cap, peek(lex));
		if (!buf)
			return -1;
		advance(lex);
	}
	if (peek(lex) != '\'') {
		free(buf);
		return -2; /* unterminated */
	}
	advance(lex); /* closing ' */
	buf = sh_buf_push(buf, &len, &cap, '\0');
	if (!buf)
		return -1;
	*out = buf;
	return 0;
}

static int read_double_quoted(struct sh_lexer *lex, char **out)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;

	advance(lex); /* opening " */
	while (!at_eof(lex) && peek(lex) != '"') {
		if (peek(lex) == '\\') {
			char n;

			advance(lex);
			if (at_eof(lex)) {
				free(buf);
				return -2;
			}
			n = peek(lex);
			if (n == '\n') {
				free(buf);
				return -2;
			}
			if (n == '"' || n == '\\' || n == '$' || n == '`') {
				advance(lex);
				buf = sh_buf_push(buf, &len, &cap, n);
			} else {
				buf = sh_buf_push(buf, &len, &cap, '\\');
				buf = sh_buf_push(buf, &len, &cap, n);
				advance(lex);
			}
			if (!buf)
				return -1;
		} else if (peek(lex) == '$') {
			int consumed;

			if (expand_variable(lex, &buf, &len, &cap, &consumed) !=
			    0)
				return -1;
			if (!consumed) {
				buf = sh_buf_push(buf, &len, &cap, '$');
				if (!buf)
					return -1;
				advance(lex);
			}
		} else {
			buf = sh_buf_push(buf, &len, &cap, peek(lex));
			if (!buf)
				return -1;
			advance(lex);
		}
	}
	if (peek(lex) != '"') {
		free(buf);
		return -2;
	}
	advance(lex);
	buf = sh_buf_push(buf, &len, &cap, '\0');
	if (!buf)
		return -1;
	*out = buf;
	return 0;
}

static int read_word(struct sh_lexer *lex, char **out)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;

	while (!at_eof(lex)) {
		char c = peek(lex);

		if (c == '\'') {
			char *inner;
			int r = read_single_quoted(lex, &inner);

			if (r)
				goto fail;
			for (size_t i = 0; inner[i]; i++) {
				buf = sh_buf_push(buf, &len, &cap, inner[i]);
				if (!buf) {
					free(inner);
					return -1;
				}
			}
			free(inner);
			continue;
		}
		if (c == '"') {
			char *inner;
			int r = read_double_quoted(lex, &inner);

			if (r)
				goto fail;
			for (size_t i = 0; inner[i]; i++) {
				buf = sh_buf_push(buf, &len, &cap, inner[i]);
				if (!buf) {
					free(inner);
					return -1;
				}
			}
			free(inner);
			continue;
		}
		if (isspace((unsigned char)c) || c == '|' || c == '<' ||
		    c == '>' || c == '#' || c == '\0')
			break;
		if (c == '$') {
			int consumed;

			if (expand_variable(lex, &buf, &len, &cap, &consumed) !=
			    0)
				return -1;
			if (!consumed) {
				buf = sh_buf_push(buf, &len, &cap, '$');
				if (!buf)
					return -1;
				advance(lex);
			}
			continue;
		}
		if (c == '\\' && lex->pos + 1 < lex->len) {
			advance(lex);
			c = peek(lex);
		}
		buf = sh_buf_push(buf, &len, &cap, c);
		if (!buf)
			return -1;
		advance(lex);
	}
	buf = sh_buf_push(buf, &len, &cap, '\0');
	if (!buf)
		return -1;
	*out = buf;
	return 0;
fail:
	free(buf);
	return -2;
}

sh_err_t sh_lexer_next(struct sh_lexer *lex, struct sh_token *tok)
{
next:
	tok->type = SH_TOKEN_EOF;
	tok->text = NULL;

	skip_spaces(lex);
	if (at_eof(lex))
		return SH_OK;

	if (peek(lex) == '\n') {
		advance(lex);
		tok->type = SH_TOKEN_NEWLINE;
		return SH_OK;
	}

	if (peek(lex) == '#') {
		skip_comment(lex);
		goto next; /* avoid recursive call */
	}

	if (peek(lex) == '|') {
		advance(lex);
		tok->type = SH_TOKEN_PIPE;
		return SH_OK;
	}

	if (peek(lex) == '<') {
		advance(lex);
		tok->type = SH_TOKEN_REDIR_IN;
		return SH_OK;
	}

	if (peek(lex) == '>') {
		advance(lex);
		if (peek(lex) == '>') {
			advance(lex);
			tok->type = SH_TOKEN_REDIR_APPEND;
		} else {
			tok->type = SH_TOKEN_REDIR_OUT;
		}
		return SH_OK;
	}

	int rw = read_word(lex, &tok->text);
	if (rw != 0) {
		sh_err_t e;

		sh_token_release(tok);
		tok->type = SH_TOKEN_EOF;
		if (rw == -2) {
			snprintf(lex->errmsg, sizeof(lex->errmsg),
				 "unterminated quote");
			e = SH_ERR_LEX;
		} else {
			snprintf(lex->errmsg, sizeof(lex->errmsg),
				 "out of memory");
			e = SH_ERR_NOMEM;
		}
		return e;
	}
	tok->type = SH_TOKEN_WORD;
	return SH_OK;
}
