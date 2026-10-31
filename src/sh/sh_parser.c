#include "sh_parser.h"
#include "sh_lexer.h"
#include "sh_pipeline.h"
#include <stdlib.h>
#include <string.h>

static int argv_push(struct sh_simple_cmd *c, char *w)
{
	char **na = realloc(c->argv, (c->argc + 2) * sizeof(char *));

	if (!na)
		return -1;
	c->argv = na;
	c->argv[c->argc++] = w;
	c->argv[c->argc] = NULL;
	return 0;
}

static void parse_fail(struct sh_pipeline *out, struct sh_parse_result *res,
		       sh_err_t err, const char *msg)
{
	res->err = err;
	res->errmsg = msg;
	sh_pipeline_release(out);
	sh_pipeline_init(out);
}

static struct sh_simple_cmd *tail_cmd(struct sh_pipeline *p)
{
	if (p->nr_cmds == 0)
		return NULL;
	return &p->cmds[p->nr_cmds - 1];
}

static int cur_has_program(struct sh_pipeline *p)
{
	struct sh_simple_cmd *c = tail_cmd(p);

	return c && c->argc > 0;
}

static int push_cmd(struct sh_pipeline *p)
{
	struct sh_simple_cmd *n;

	n = realloc(p->cmds, (p->nr_cmds + 1) * sizeof(*n));
	if (!n)
		return -1;
	p->cmds = n;

	struct sh_simple_cmd *c = &p->cmds[p->nr_cmds++];

	c->argv = NULL;
	c->argc = 0;
	c->redir_list = NULL;

	return 0;
}

static int ensure_cmd(struct sh_pipeline *p)
{
	if (p->nr_cmds == 0)
		return push_cmd(p);
	return 0;
}

static int redir_append(struct sh_simple_cmd *c, enum sh_redir_kind k,
			char *path)
{
	struct sh_redir *r = malloc(sizeof(*r));

	if (!r)
		return -1;
	r->kind = k;
	r->path = path;
	r->next = NULL;
	if (!c->redir_list) {
		c->redir_list = r;
	} else {
		struct sh_redir *x = c->redir_list;

		while (x->next)
			x = x->next;
		x->next = r;
	}
	return 0;
}

sh_err_t sh_parse_line(struct sh_lexer *lex, struct sh_pipeline *out,
		       struct sh_parse_result *res)
{
	sh_err_t le;

	res->err = SH_OK;
	res->errmsg = NULL;
	sh_pipeline_init(out);

	for (;;) {
		struct sh_token t;

		memset(&t, 0, sizeof(t));
		le = sh_lexer_next(lex, &t);
		if (le != SH_OK) {
			const char *msg = lex->errmsg[0] ? (const char *)lex->errmsg :
							   sh_err_default_msg(le);

			parse_fail(out, res, le, msg);
			return le;
		}

		if (t.type == SH_TOKEN_NEWLINE || t.type == SH_TOKEN_EOF) {
			sh_token_release(&t);
			break;
		}

		if (t.type == SH_TOKEN_PIPE) {
			sh_token_release(&t);
			if (!cur_has_program(out)) {
				parse_fail(out, res, SH_ERR_PARSE,
					   "syntax error near unexpected token `|'");
				return SH_ERR_PARSE;
			}
			if (push_cmd(out)) {
				parse_fail(out, res, SH_ERR_NOMEM,
					   sh_err_default_msg(SH_ERR_NOMEM));
				return SH_ERR_NOMEM;
			}
			continue;
		}

		if (t.type == SH_TOKEN_REDIR_IN ||
		    t.type == SH_TOKEN_REDIR_OUT ||
		    t.type == SH_TOKEN_REDIR_APPEND) {
			enum sh_token_type rk = t.type;

			sh_token_release(&t);
			if (ensure_cmd(out)) {
				parse_fail(out, res, SH_ERR_NOMEM,
					   sh_err_default_msg(SH_ERR_NOMEM));
				return SH_ERR_NOMEM;
			}

			struct sh_token path;

			memset(&path, 0, sizeof(path));
			le = sh_lexer_next(lex, &path);
			if (le != SH_OK) {
				const char *msg =
					lex->errmsg[0] ? (const char *)lex->errmsg :
						       sh_err_default_msg(le);

				parse_fail(out, res, le, msg);
				return le;
			}
			if (path.type != SH_TOKEN_WORD) {
				sh_token_release(&path);
				parse_fail(out, res, SH_ERR_PARSE,
					   "syntax error: redirection needs a filename");
				return SH_ERR_PARSE;
			}

			enum sh_redir_kind k = SH_REDIR_IN;

			if (rk == SH_TOKEN_REDIR_OUT)
				k = SH_REDIR_OUT;
			else if (rk == SH_TOKEN_REDIR_APPEND)
				k = SH_REDIR_APPEND;
			if (redir_append(tail_cmd(out), k, path.text)) {
				sh_token_release(&path);
				parse_fail(out, res, SH_ERR_NOMEM,
					   sh_err_default_msg(SH_ERR_NOMEM));
				return SH_ERR_NOMEM;
			}
			path.text = NULL;

			sh_token_release(&path);

			continue;
		}

		if (t.type == SH_TOKEN_WORD) {
			if (ensure_cmd(out)) {
				sh_token_release(&t);
				parse_fail(out, res, SH_ERR_NOMEM,
					   sh_err_default_msg(SH_ERR_NOMEM));
				return SH_ERR_NOMEM;
			}
			if (argv_push(tail_cmd(out), t.text)) {
				sh_token_release(&t);
				parse_fail(out, res, SH_ERR_NOMEM,
					   sh_err_default_msg(SH_ERR_NOMEM));
				return SH_ERR_NOMEM;
			}
			t.text = NULL;
			sh_token_release(&t);
			continue;
		}

		sh_token_release(&t);
		parse_fail(out, res, SH_ERR_PARSE,
			   "syntax error: unexpected token");
		return SH_ERR_PARSE;
	}

	if (out->nr_cmds == 0)
		return SH_OK;

	struct sh_simple_cmd *last = tail_cmd(out);

	if (last->argc == 0) {
		parse_fail(out, res, SH_ERR_PARSE,
			   "syntax error: missing command");
		return SH_ERR_PARSE;
	}

	return SH_OK;
}
