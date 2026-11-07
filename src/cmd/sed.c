#include <brk/cmd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] = "Usage: sed [-n] [-e SCRIPT] [SCRIPT] [FILE...]\n"
			    "Stream editor (basic s///, p, d and q commands).\n"
			    "\n"
			    "Options:\n"
			    "  -n       suppress automatic printing\n"
			    "  -e SCRIPT  add SCRIPT to the command list\n"
			    "  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 'n', 'n', "quiet", BRK_CMD_OPT_NO_ARG },
	{ 'e', 'e', "expression", BRK_CMD_OPT_REQUIRED_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

enum sed_cmd_type {
	SED_SUBST,
	SED_PRINT,
	SED_DELETE,
	SED_QUIT,
};

struct sed_cmd {
	enum sed_cmd_type type;
	char *pattern;
	char *replacement;
	bool global;
	bool print;
};

struct sed_prog {
	struct sed_cmd *cmds;
	int count;
	int cap;
};

static bool append_cmd(struct sed_prog *p, struct sed_cmd c)
{
	if (p->count == p->cap) {
		int ncap = p->cap ? p->cap * 2 : 8;
		struct sed_cmd *n = realloc(p->cmds, (size_t)ncap * sizeof(*n));

		if (!n)
			return false;
		p->cmds = n;
		p->cap = ncap;
	}
	p->cmds[p->count++] = c;
	return true;
}

static bool atom_match(const char *pat, char c, int *len)
{
	if (pat[0] == '\\' && pat[1]) {
		*len = 2;
		return c == pat[1];
	}
	if (pat[0] == '.') {
		*len = 1;
		return true;
	}
	if (pat[0] == '[') {
		const char *p = pat + 1;
		bool negate = false;
		bool matched = false;

		if (*p == '!' || *p == '^') {
			negate = true;
			p++;
		}
		while (*p && *p != ']') {
			char lo = *p++;
			char hi = lo;

			if (*p == '-' && p[1] && p[1] != ']') {
				p++;
				hi = *p++;
			}
			if (c >= lo && c <= hi)
				matched = true;
		}
		if (*p != ']')
			return false;
		*len = (int)(p - pat + 1);
		return matched != negate;
	}
	*len = 1;
	return c == pat[0];
}

static int match_here(const char *pat, const char *text)
{
	int alen = 1;

	if (!*pat)
		return 0;
	if (pat[0] == '$' && pat[1] == '\0')
		return *text ? -1 : 0;

	atom_match(pat, '\0', &alen);
	if (pat[alen] == '*') {
		int max = 0;

		while (text[max] && atom_match(pat, text[max], &alen))
			max++;
		for (int i = max; i >= 0; i--) {
			int r = match_here(pat + alen + 1, text + i);

			if (r >= 0)
				return i + r;
		}
		return -1;
	}

	if (*text && atom_match(pat, *text, &alen)) {
		int r = match_here(pat + alen, text + 1);

		return r >= 0 ? 1 + r : -1;
	}
	return -1;
}

static bool regex_match(const char *pat, const char *text, int *start, int *end)
{
	size_t len = strlen(text);
	bool anchored = pat[0] == '^';

	if (anchored)
		pat++;
	for (size_t i = anchored ? 0 : 0; i <= len; i++) {
		int r = match_here(pat, text + i);

		if (r >= 0) {
			*start = (int)i;
			*end = (int)i + r;
			return true;
		}
		if (anchored)
			break;
	}
	return false;
}

struct dynbuf {
	char *data;
	size_t len;
	size_t cap;
};

static bool dyn_append(struct dynbuf *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		size_t ncap = b->cap ? b->cap : 64;

		while (ncap < b->len + n + 1)
			ncap *= 2;
		char *p = realloc(b->data, ncap);

		if (!p)
			return false;
		b->data = p;
		b->cap = ncap;
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return true;
}

static char *expand_replacement(const char *repl, const char *match,
				size_t match_len, size_t *out_len)
{
	struct dynbuf b = { 0 };

	for (const char *p = repl; *p; p++) {
		if (*p == '&') {
			if (!dyn_append(&b, match, match_len))
				goto fail;
		} else if (*p == '\\' && p[1]) {
			p++;
			char c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p;

			if (!dyn_append(&b, &c, 1))
				goto fail;
		} else if (!dyn_append(&b, p, 1)) {
			goto fail;
		}
	}
	*out_len = b.len;
	return b.data;
fail:
	free(b.data);
	return NULL;
}

static char *substitute(const char *line, const struct sed_cmd *cmd)
{
	const char *p = line;
	struct dynbuf out = { 0 };
	bool changed = false;

	while (*p || !changed) {
		int start, end;

		if (!regex_match(cmd->pattern, p, &start, &end))
			break;
		if (!dyn_append(&out, p, (size_t)start))
			goto fail;

		size_t rlen;
		char *expanded = expand_replacement(cmd->replacement, p + start,
						    (size_t)(end - start),
						    &rlen);

		if (!expanded)
			goto fail;
		if (!dyn_append(&out, expanded, rlen)) {
			free(expanded);
			goto fail;
		}
		free(expanded);
		changed = true;
		p += end;
		if (end == 0) {
			if (!*p)
				break;
			if (!dyn_append(&out, p, 1))
				goto fail;
			p++;
		}
		if (!cmd->global)
			break;
	}
	if (!changed) {
		free(out.data);
		return NULL;
	}
	if (!dyn_append(&out, p, strlen(p)))
		goto fail;
	return out.data;
fail:
	free(out.data);
	return NULL;
}

static bool parse_subst(const char **sp, struct sed_cmd *cmd)
{
	const char *p = *sp;
	char delim = *p++;

	if (!delim || delim == '\n')
		return false;

	struct dynbuf pat = { 0 };
	struct dynbuf repl = { 0 };

	while (*p && *p != delim) {
		if (*p == '\\' && p[1] == delim) {
			if (!dyn_append(&pat, &p[1], 1))
				goto fail;
			p += 2;
		} else if (!dyn_append(&pat, p++, 1)) {
			goto fail;
		}
	}
	if (*p != delim)
		goto fail;
	p++;

	while (*p && *p != delim) {
		if (*p == '\\' && p[1] == delim) {
			if (!dyn_append(&repl, &p[1], 1))
				goto fail;
			p += 2;
		} else if (!dyn_append(&repl, p++, 1)) {
			goto fail;
		}
	}
	if (*p != delim)
		goto fail;
	p++;

	cmd->pattern = pat.data ? pat.data : strdup("");
	cmd->replacement = repl.data ? repl.data : strdup("");
	if (!cmd->pattern || !cmd->replacement)
		goto fail;
	while (*p && *p != ';' && *p != '\n') {
		if (*p == 'g')
			cmd->global = true;
		else if (*p == 'p')
			cmd->print = true;
		p++;
	}
	*sp = p;
	return true;
fail:
	free(pat.data);
	free(repl.data);
	return false;
}

static bool parse_script(const char *s, struct sed_prog *prog)
{
	while (*s) {
		while (*s == ' ' || *s == '\t' || *s == ';' || *s == '\n')
			s++;
		if (!*s)
			break;
		if (*s == '#') {
			while (*s && *s != '\n')
				s++;
			continue;
		}

		struct sed_cmd cmd = { 0 };

		if (*s == 's') {
			s++;
			cmd.type = SED_SUBST;
			if (!parse_subst(&s, &cmd))
				return false;
		} else if (*s == 'p') {
			cmd.type = SED_PRINT;
			s++;
		} else if (*s == 'd') {
			cmd.type = SED_DELETE;
			s++;
		} else if (*s == 'q') {
			cmd.type = SED_QUIT;
			s++;
		} else {
			return false;
		}
		if (!append_cmd(prog, cmd))
			return false;
	}
	return true;
}

static void free_prog(struct sed_prog *prog)
{
	for (int i = 0; i < prog->count; i++) {
		free(prog->cmds[i].pattern);
		free(prog->cmds[i].replacement);
	}
	free(prog->cmds);
}

static bool quit;

static void run_stream(FILE *fp, const struct sed_prog *prog, bool quiet)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t n;

	while (!quit && (n = getline(&line, &cap, fp)) >= 0) {
		size_t len = (size_t)n;
		bool deleted = false;

		while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		char *current = strdup(line);

		if (!current)
			break;

		for (int i = 0; i < prog->count && !deleted; i++) {
			const struct sed_cmd *cmd = &prog->cmds[i];

			switch (cmd->type) {
			case SED_SUBST: {
				char *next = substitute(current, cmd);

				if (next) {
					free(current);
					current = next;
					if (cmd->print)
						puts(current);
				}
				break;
			}
			case SED_PRINT:
				puts(current);
				break;
			case SED_DELETE:
				deleted = true;
				break;
			case SED_QUIT:
				quit = true;
				break;
			}
		}

		if (!quiet && !deleted)
			puts(current);
		free(current);
	}
	free(line);
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	struct sed_prog prog = { 0 };
	bool quiet = false;
	char *script = NULL;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'n')
			quiet = true;
		if (opt == 'e') {
			size_t old = script ? strlen(script) : 0;
			size_t add = strlen(ctx.arg);
			char *n = realloc(script, old + add + 2);

			if (!n)
				return brk_cmd_fail("out of memory");
			script = n;
			if (old)
				script[old++] = '\n';
			memcpy(script + old, ctx.arg, add + 1);
		}
	}

	if (!script) {
		if (brk_cmd_operand_count(&ctx) == 0)
			brk_cmd_usage(usage);
		script = strdup(brk_cmd_operand(&ctx, 0));
		if (!script)
			return brk_cmd_fail("out of memory");
		ctx.index++;
	}

	if (!parse_script(script, &prog)) {
		free(script);
		free_prog(&prog);
		return brk_cmd_fail("invalid sed script");
	}
	free(script);

	if (brk_cmd_operand_count(&ctx) == 0) {
		run_stream(stdin, &prog, quiet);
	} else {
		for (int i = 0; i < brk_cmd_operand_count(&ctx) && !quit; i++) {
			const char *path = brk_cmd_operand(&ctx, i);
			FILE *fp = !strcmp(path, "-") ? stdin :
							fopen(path, "r");

			if (!fp) {
				brk_cmd_error_errno(path);
				continue;
			}
			run_stream(fp, &prog, quiet);
			if (fp != stdin)
				fclose(fp);
		}
	}

	free_prog(&prog);
	return BRK_CMD_EXIT_OK;
}
