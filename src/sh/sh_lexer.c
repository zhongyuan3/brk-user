#include "sh_lexer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

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

static char *buf_push(char *buf, size_t *len, size_t *cap, char c)
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

static int read_single_quoted(struct sh_lexer *lex, char **out)
{
	char *buf = NULL;
	size_t len = 0, cap = 0;

	advance(lex); /* opening ' */
	while (!at_eof(lex) && peek(lex) != '\'') {
		buf = buf_push(buf, &len, &cap, peek(lex));
		if (!buf)
			return -1;
		advance(lex);
	}
	if (peek(lex) != '\'') {
		free(buf);
		return -2; /* unterminated */
	}
	advance(lex); /* closing ' */
	buf = buf_push(buf, &len, &cap, '\0');
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
				buf = buf_push(buf, &len, &cap, n);
			} else {
				buf = buf_push(buf, &len, &cap, '\\');
				buf = buf_push(buf, &len, &cap, n);
				advance(lex);
			}
		} else {
			buf = buf_push(buf, &len, &cap, peek(lex));
			advance(lex);
		}
		if (!buf)
			return -1;
	}
	if (peek(lex) != '"') {
		free(buf);
		return -2;
	}
	advance(lex);
	buf = buf_push(buf, &len, &cap, '\0');
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
				buf = buf_push(buf, &len, &cap, inner[i]);
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
				buf = buf_push(buf, &len, &cap, inner[i]);
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
		if (c == '\\' && lex->pos + 1 < lex->len) {
			advance(lex);
			c = peek(lex);
		}
		buf = buf_push(buf, &len, &cap, c);
		if (!buf)
			return -1;
		advance(lex);
	}
	buf = buf_push(buf, &len, &cap, '\0');
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
