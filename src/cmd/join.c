#include <brk/cmd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] =
	"Usage: join [-1 FIELD] [-2 FIELD] [-t CHAR] FILE1 FILE2\n"
	"Join lines of two files on a common field.";

static const struct brk_cmd_option opts[] = {
	{ '1', '1', NULL, BRK_CMD_OPT_REQUIRED_ARG },
	{ '2', '2', NULL, BRK_CMD_OPT_REQUIRED_ARG },
	{ 't', 't', NULL, BRK_CMD_OPT_REQUIRED_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

#define JOIN_MAX_FIELDS 64

struct jline {
	char *raw;
	char *fields[JOIN_MAX_FIELDS];
	int count;
};

static bool is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

static void split_line(struct jline *l, char delim)
{
	char *p = l->raw;

	l->count = 0;
	if (delim) {
		while (l->count < JOIN_MAX_FIELDS) {
			l->fields[l->count++] = p;
			char *next = strchr(p, delim);

			if (!next)
				break;
			*next = '\0';
			p = next + 1;
		}
		return;
	}

	while (*p) {
		while (*p && is_space(*p))
			p++;
		if (!*p)
			break;
		if (l->count >= JOIN_MAX_FIELDS)
			break;
		l->fields[l->count++] = p;
		while (*p && !is_space(*p))
			p++;
		if (*p)
			*p++ = '\0';
	}
}

static int load(const char *path, struct jline **out, size_t *count_out,
		char delim)
{
	FILE *fp = !strcmp(path, "-") ? stdin : fopen(path, "r");
	struct jline *lines = NULL;
	size_t count = 0, cap = 0;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t n;

	if (!fp)
		return -1;

	while ((n = getline(&line, &linecap, fp)) >= 0) {
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = '\0';
		if (count == cap) {
			size_t ncap = cap ? cap * 2 : 64;
			struct jline *nl = realloc(lines, ncap * sizeof(*nl));

			if (!nl) {
				free(line);
				goto fail;
			}
			lines = nl;
			cap = ncap;
		}
		lines[count].raw = strdup(line);
		if (!lines[count].raw) {
			free(line);
			goto fail;
		}
		split_line(&lines[count], delim);
		count++;
	}
	free(line);
	if (fp != stdin)
		fclose(fp);
	*out = lines;
	*count_out = count;
	return 0;

fail:
	free(line);
	for (size_t i = 0; i < count; i++)
		free(lines[i].raw);
	free(lines);
	if (fp != stdin)
		fclose(fp);
	return -1;
}

static const char *field(const struct jline *l, int n)
{
	if (n < 1 || n > l->count)
		return NULL;
	return l->fields[n - 1];
}

static void put_fields(const struct jline *l, int skip, const char *sep,
		       bool *first)
{
	for (int i = 0; i < l->count; i++) {
		if (i + 1 == skip)
			continue;
		if (!*first)
			fputs(sep, stdout);
		fputs(l->fields[i], stdout);
		*first = false;
	}
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	int f1 = 1, f2 = 1;
	char delim = 0;
	char sepbuf[2] = { 0, 0 };
	const char *sep = " ";
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == '1') {
			long v;

			if (brk_cmd_parse_long(ctx.arg, &v, 1,
					       JOIN_MAX_FIELDS) != 0)
				brk_cmd_usage(usage);
			f1 = (int)v;
		}
		if (opt == '2') {
			long v;

			if (brk_cmd_parse_long(ctx.arg, &v, 1,
					       JOIN_MAX_FIELDS) != 0)
				brk_cmd_usage(usage);
			f2 = (int)v;
		}
		if (opt == 't') {
			if (!ctx.arg || !ctx.arg[0])
				brk_cmd_usage(usage);
			delim = ctx.arg[0];
			if (delim == '\\' && ctx.arg[1] == 't')
				delim = '\t';
			sepbuf[0] = delim;
			sepbuf[1] = '\0';
			sep = sepbuf;
		}
	}

	brk_cmd_require_operand_count(&ctx, 2, usage);
	const char *pa = brk_cmd_operand(&ctx, 0);
	const char *pb = brk_cmd_operand(&ctx, 1);
	struct jline *a = NULL, *b = NULL;
	size_t na = 0, nb = 0;

	if (load(pa, &a, &na, delim) != 0)
		return brk_cmd_fail_errno(pa);
	if (load(pb, &b, &nb, delim) != 0) {
		for (size_t i = 0; i < na; i++)
			free(a[i].raw);
		free(a);
		return brk_cmd_fail_errno(pb);
	}

	for (size_t i = 0; i < na; i++) {
		const char *key = field(&a[i], f1);

		if (!key)
			continue;
		for (size_t j = 0; j < nb; j++) {
			const char *other = field(&b[j], f2);

			if (!other || strcmp(key, other))
				continue;

			bool first = true;

			fputs(key, stdout);
			first = false;
			put_fields(&a[i], f1, sep, &first);
			put_fields(&b[j], f2, sep, &first);
			putchar('\n');
		}
	}

	for (size_t i = 0; i < na; i++)
		free(a[i].raw);
	for (size_t i = 0; i < nb; i++)
		free(b[i].raw);
	free(a);
	free(b);
	return ret;
}
