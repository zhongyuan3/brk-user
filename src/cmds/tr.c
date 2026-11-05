#include <brk/cmd.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

static const char help[] =
	"Usage: tr [-d] [-s] SET1 [SET2]\n"
	"Translate, delete or squeeze characters from standard input.\n"
	"\n"
	"Options:\n"
	"  -d       delete characters in SET1\n"
	"  -s       squeeze repeated output characters\n"
	"  --help   display this help and exit";

struct tr_set {
	unsigned char v[256];
	size_t len;
};

static int set_add(struct tr_set *set, unsigned char c)
{
	for (size_t i = 0; i < set->len; i++)
		if (set->v[i] == c)
			return 0;
	if (set->len == sizeof(set->v))
		return -1;
	set->v[set->len++] = c;
	return 0;
}

static int hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int parse_char(const char **sp, unsigned char *out)
{
	const char *s = *sp;
	char c = *s++;

	if (c != '\\') {
		*out = (unsigned char)c;
		*sp = s;
		return 0;
	}

	if (!*s) {
		*out = '\\';
		*sp = s;
		return 0;
	}

	c = *s++;
	switch (c) {
	case 'a':
		*out = '\a';
		break;
	case 'b':
		*out = '\b';
		break;
	case 'f':
		*out = '\f';
		break;
	case 'n':
		*out = '\n';
		break;
	case 'r':
		*out = '\r';
		break;
	case 't':
		*out = '\t';
		break;
	case 'v':
		*out = '\v';
		break;
	case '\\':
		*out = '\\';
		break;
	case 'x': {
		int v = 0;
		int digits = 0;

		while (digits < 2 && hex_value(*s) >= 0) {
			v = v * 16 + hex_value(*s++);
			digits++;
		}
		if (!digits)
			return -1;
		*out = (unsigned char)v;
		break;
	}
	default:
		if (c >= '0' && c <= '7') {
			int v = c - '0';
			int digits = 1;

			while (digits < 3 && *s >= '0' && *s <= '7') {
				v = v * 8 + (*s++ - '0');
				digits++;
			}
			*out = (unsigned char)v;
		} else {
			*out = (unsigned char)c;
		}
		break;
	}

	*sp = s;
	return 0;
}

static int parse_set(const char *s, struct tr_set *set)
{
	const char *start = s;

	while (*s) {
		unsigned char first;

		if (s == start && *s == '-') {
			if (set_add(set, '-') != 0)
				return -1;
			s++;
			continue;
		}

		if (parse_char(&s, &first) != 0)
			return -1;

		if (*s == '-' && s[1] != '\0') {
			unsigned char last;

			s++;
			if (parse_char(&s, &last) != 0 || last < first)
				return -1;
			for (unsigned int c = first; c <= last; c++)
				if (set_add(set, (unsigned char)c) != 0)
					return -1;
		} else {
			if (set_add(set, first) != 0)
				return -1;
		}
	}
	return set->len ? 0 : -1;
}

static int write_all(const unsigned char *buf, size_t len)
{
	while (len) {
		ssize_t n = write(STDOUT_FILENO, buf, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			return -1;
		buf += n;
		len -= (size_t)n;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	struct tr_set set1 = { 0 };
	struct tr_set set2 = { 0 };
	unsigned char map[256];
	bool in_set1[256] = { false };
	bool squeeze[256] = { false };
	bool delete_chars = false;
	bool squeeze_chars = false;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	static const struct brk_cmd_option opts[] = {
		{ 'd', 'd', NULL, BRK_CMD_OPT_NO_ARG },
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
			delete_chars = true;
		if (opt == 's')
			squeeze_chars = true;
	}

	if (brk_cmd_operand_count(&ctx) < 1 || brk_cmd_operand_count(&ctx) > 2)
		brk_cmd_usage(help);
	if (delete_chars && brk_cmd_operand_count(&ctx) != 1)
		brk_cmd_usage(help);
	if (!delete_chars && !squeeze_chars && brk_cmd_operand_count(&ctx) != 2)
		brk_cmd_usage(help);

	if (parse_set(brk_cmd_operand(&ctx, 0), &set1) != 0)
		return brk_cmd_fail("invalid SET1");

	if (brk_cmd_operand_count(&ctx) == 2 &&
	    parse_set(brk_cmd_operand(&ctx, 1), &set2) != 0)
		return brk_cmd_fail("invalid SET2");

	for (unsigned int i = 0; i < 256; i++)
		map[i] = (unsigned char)i;

	for (size_t i = 0; i < set1.len; i++)
		in_set1[set1.v[i]] = true;

	if (!delete_chars && set2.len) {
		for (size_t i = 0; i < set1.len; i++)
			map[set1.v[i]] =
				set2.v[i < set2.len ? i : set2.len - 1];
	}

	if (squeeze_chars) {
		const struct tr_set *squeeze_set = set2.len ? &set2 : &set1;

		for (size_t i = 0; i < squeeze_set->len; i++)
			squeeze[squeeze_set->v[i]] = true;
	}

	unsigned char in[4096];
	unsigned char out[4096];
	size_t out_len = 0;
	unsigned char previous = 0;
	bool have_previous = false;

	for (;;) {
		ssize_t n = read(STDIN_FILENO, in, sizeof(in));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return brk_cmd_fail_errno("read");
		}
		if (n == 0)
			break;

		for (ssize_t i = 0; i < n; i++) {
			unsigned char c = in[i];

			if (delete_chars && in_set1[c])
				continue;

			unsigned char o = delete_chars ? c : map[c];

			if (squeeze_chars && squeeze[o] && have_previous &&
			    previous == o)
				continue;

			out[out_len++] = o;
			previous = o;
			have_previous = true;

			if (out_len == sizeof(out)) {
				if (write_all(out, out_len) != 0)
					return brk_cmd_fail_errno("write");
				out_len = 0;
			}
		}
	}

	if (out_len && write_all(out, out_len) != 0)
		return brk_cmd_fail_errno("write");
	return BRK_CMD_EXIT_OK;
}
