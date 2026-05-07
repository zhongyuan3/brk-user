#ifndef SH_PARSER_H
#define SH_PARSER_H

#include "sh_err.h"
#include "sh_lexer.h"
#include "sh_pipeline.h"

struct sh_parse_result {
	sh_err_t err;
	const char *errmsg;
};

sh_err_t sh_parse_line(struct sh_lexer *lex, struct sh_pipeline *out,
		       struct sh_parse_result *res);

#endif
