#include <brk/cmd.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char help[] =
	"Usage: cut (-f LIST | -c LIST) [-d DELIM] [-s] [FILE...]\n"
	"Select fields or characters from each line.\n"
	"\n"
	"Options:\n"
	"  -d DELIM  field delimiter (default: tab)\n"
	"  -f LIST   select fields, e.g. 1,3-5\n"
	"  -c LIST   select character positions, e.g. 1,3-5\n"
	"  -s        suppress lines that contain no delimiter\n"
	"  --help    display this help and exit";

struct range {
	unsigned long first;
	unsigned long last; /* 0 means unbounded */
};

struct ranges {
	struct range *items;
	size_t len;
	size_t cap;
};

static int range_add(struct ranges *ranges, unsigned long first,
		     unsigned long last)
{
	if (ranges->len == ranges->cap) {
		size_t cap = ranges->cap ? ranges->cap * 2 : 8;
		struct range *items =
			realloc(ranges->items, cap * sizeof(*items));

		if (!items)
			return -1;
		ranges->items = items;
		ranges->cap = cap;
	}
	ranges->items[ranges->len].first = first;
	ranges->items[ranges->len].last = last;
	ranges->len++;
	return 0;
}

static int parse_list(const char *list, struct ranges *ranges)
{
	char *copy = strdup(list);
	char *p;

	if (!copy)
		return -1;

	p = copy;
	while (p) {
		char *comma = strchr(p, ',');
		char *dash;
		char *end;
		unsigned long first;
		unsigned long last;

		if (comma)
			*comma = '\0';

		if (!*p) {
			free(copy);
			return -1;
		}

		dash = strchr(p, '-');
		if (dash == p) {
			if (!p[1])
				goto bad;
			last = strtoul(p + 1, &end, 10);
			if (*end || last == 0)
				goto bad;
			first = 1;
		} else if (dash) {
			*dash = '\0';
			if (!dash[1])
				last = 0;
			else {
				last = strtoul(dash + 1, &end, 10);
				if (*end || last == 0)
					goto bad;
			}
			first = strtoul(p, &end, 10);
			if (*end || first == 0)
				goto bad;
		} else {
			first = strtoul(p, &end, 10);
			if (*end || first == 0)
				goto bad;
			last = first;
		}
		if (last && last < first)
			goto bad;
		if (range_add(ranges, first, last) != 0)
			goto bad;

		p = comma ? comma + 1 : NULL;
	}

	free(copy);
	return 0;
bad:
	free(copy);
	return -1;
}

static bool selected(const struct ranges *ranges, unsigned long index)
{
	for (size_t i = 0; i < ranges->len; i++) {
		if (index >= ranges->items[i].first &&
		    (!ranges->items[i].last || index <= ranges->items[i].last))
			return true;
	}
	return false;
}

static int parse_delim(const char *arg, char *delim)
{
	if (!arg[0] || arg[1] == '\0') {
		*delim = arg[0];
		return arg[0] ? 0 : -1;
	}
	if (arg[0] == '\\' && arg[2] == '\0') {
		switch (arg[1]) {
		case 't':
			*delim = '\t';
			return 0;
		case 'n':
			*delim = '\n';
			return 0;
		case 'r':
			*delim = '\r';
			return 0;
		case '\\':
			*delim = '\\';
			return 0;
		}
	}
	return -1;
}

static int cut_file(FILE *f, const struct ranges *field_ranges,
		    const struct ranges *char_ranges, char delim, bool suppress)
{
	char *line = NULL;
	size_t cap = 0;
	ssize_t len;

	while ((len = getline(&line, &cap, f)) != -1) {
		while (len > 0 &&
		       (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';

		if (char_ranges) {
			for (ssize_t i = 0; i < len; i++)
				if (selected(char_ranges, (unsigned long)i + 1))
					putchar((unsigned char)line[i]);
			putchar('\n');
			continue;
		}

		if (!strchr(line, delim)) {
			if (!suppress)
				puts(line);
			continue;
		}

		char *p = line;
		unsigned long field = 1;
		bool first = true;

		for (;;) {
			char *sep = strchr(p, delim);
			size_t flen = sep ? (size_t)(sep - p) : strlen(p);

			if (selected(field_ranges, field)) {
				if (!first)
					putchar(delim);
				fwrite(p, 1, flen, stdout);
				first = false;
			}
			if (!sep)
				break;
			p = sep + 1;
			field++;
		}
		putchar('\n');
	}

	free(line);
	return ferror(f) ? -1 : 0;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	struct ranges field_ranges = { 0 };
	struct ranges char_ranges = { 0 };
	const char *field_list = NULL;
	const char *char_list = NULL;
	const char *delim_arg = NULL;
	bool suppress = false;
	bool have_fields = false;
	bool have_chars = false;
	char delim = '\t';
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	static const struct brk_cmd_option opts[] = {
		{ 'd', 'd', NULL, BRK_CMD_OPT_REQUIRED_ARG },
		{ 'f', 'f', NULL, BRK_CMD_OPT_REQUIRED_ARG },
		{ 'c', 'c', NULL, BRK_CMD_OPT_REQUIRED_ARG },
		{ 's', 's', NULL, BRK_CMD_OPT_NO_ARG },
		{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
		BRK_CMD_OPTION_END,
	};

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(help);
		if (opt == 'd')
			delim_arg = ctx.arg;
		if (opt == 'f') {
			field_list = ctx.arg;
			have_fields = true;
		}
		if (opt == 'c') {
			char_list = ctx.arg;
			have_chars = true;
		}
		if (opt == 's')
			suppress = true;
	}

	if (have_fields == have_chars)
		brk_cmd_usage(help);

	if (delim_arg && parse_delim(delim_arg, &delim) != 0)
		return brk_cmd_fail("invalid delimiter");

	if (have_fields && parse_list(field_list, &field_ranges) != 0)
		return brk_cmd_fail("invalid field list");
	if (have_chars && parse_list(char_list, &char_ranges) != 0)
		return brk_cmd_fail("invalid character list");

	if (brk_cmd_operand_count(&ctx) == 0) {
		if (cut_file(stdin, have_fields ? &field_ranges : NULL,
			     have_chars ? &char_ranges : NULL, delim,
			     suppress) != 0)
			ret = brk_cmd_fail_errno("read");
	} else {
		for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
			const char *path = brk_cmd_operand(&ctx, i);
			FILE *f = !strcmp(path, "-") ? stdin : fopen(path, "r");

			if (!f) {
				ret = brk_cmd_fail_errno(path);
				continue;
			}
			if (cut_file(f, have_fields ? &field_ranges : NULL,
				     have_chars ? &char_ranges : NULL, delim,
				     suppress) != 0)
				ret = brk_cmd_fail_errno(path);
			if (f != stdin)
				fclose(f);
		}
	}

	free(field_ranges.items);
	free(char_ranges.items);
	return ret;
}
