#ifndef SH_LEXER_H
#define SH_LEXER_H

#include "sh_err.h"
#include <stddef.h>

enum sh_token_type {
	SH_TOKEN_EOF = 0,
	SH_TOKEN_WORD,
	SH_TOKEN_PIPE,
	SH_TOKEN_REDIR_IN,
	SH_TOKEN_REDIR_OUT,
	SH_TOKEN_REDIR_APPEND,
	SH_TOKEN_NEWLINE,
};

struct sh_token {
	enum sh_token_type type;
	char *text; /* owned when type == TOKEN_WORD */
};

struct sh_lexer {
	const char *input;
	size_t pos;
	size_t len;
	int line;
	int col;
	char errmsg[256];
};

void sh_lexer_init(struct sh_lexer *lex, const char *input, size_t len);
sh_err_t sh_lexer_next(struct sh_lexer *lex, struct sh_token *tok);

void sh_token_release(struct sh_token *tok);

#endif
