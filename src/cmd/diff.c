#include <brk/cmd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] = "Usage: diff [-q] FILE1 FILE2\n"
			    "Compare two files line by line.";

static const struct brk_cmd_option opts[] = {
	{ 'q', 'q', "brief", BRK_CMD_OPT_NO_ARG },
	{ 'u', 'u', "unified", BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

static int read_lines(FILE *fp, char ***lines_out, size_t *count_out)
{
	char **lines = NULL;
	size_t count = 0;
	size_t cap = 0;
	char *line = NULL;
	size_t linecap = 0;
	ssize_t n;

	while ((n = getline(&line, &linecap, fp)) >= 0) {
		while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = '\0';
		if (count == cap) {
			size_t ncap = cap ? cap * 2 : 64;
			char **nl = realloc(lines, ncap * sizeof(*nl));

			if (!nl) {
				free(line);
				for (size_t i = 0; i < count; i++)
					free(lines[i]);
				free(lines);
				return -1;
			}
			lines = nl;
			cap = ncap;
		}
		lines[count] = strdup(line);
		if (!lines[count]) {
			free(line);
			for (size_t i = 0; i < count; i++)
				free(lines[i]);
			free(lines);
			return -1;
		}
		count++;
	}
	free(line);
	*lines_out = lines;
	*count_out = count;
	return 0;
}

struct diff_op {
	char type;
	int a;
	int b;
};

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	bool brief = false;
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'q')
			brief = true;
	}

	brk_cmd_require_operand_count(&ctx, 2, usage);
	const char *pa = brk_cmd_operand(&ctx, 0);
	const char *pb = brk_cmd_operand(&ctx, 1);
	FILE *fa = !strcmp(pa, "-") ? stdin : fopen(pa, "r");
	FILE *fb = !strcmp(pb, "-") ? stdin : fopen(pb, "r");

	if (!fa)
		return brk_cmd_fail_errno(pa);
	if (!fb) {
		if (fa != stdin)
			fclose(fa);
		return brk_cmd_fail_errno(pb);
	}

	char **a = NULL, **b = NULL;
	size_t na = 0, nb = 0;

	if (read_lines(fa, &a, &na) != 0 || read_lines(fb, &b, &nb) != 0) {
		ret = brk_cmd_fail("out of memory");
		goto out;
	}

	bool same = na == nb;
	for (size_t i = 0; same && i < na; i++)
		same = !strcmp(a[i], b[i]);

	if (same)
		goto out;

	ret = BRK_CMD_EXIT_FAIL;
	if (brief) {
		printf("Files %s and %s differ\n", pa, pb);
		goto out;
	}

	size_t cells = (na + 1) * (nb + 1);

	if (nb && cells / (nb + 1) != na + 1) {
		ret = brk_cmd_fail("files too large");
		goto out;
	}
	if (cells > 16 * 1024 * 1024) {
		ret = brk_cmd_fail("files too large");
		goto out;
	}

	int *dp = calloc(cells, sizeof(*dp));

	if (!dp) {
		ret = brk_cmd_fail("out of memory");
		goto out;
	}

	for (size_t i = na; i-- > 0;) {
		for (size_t j = nb; j-- > 0;) {
			if (!strcmp(a[i], b[j]))
				dp[i * (nb + 1) + j] =
					dp[(i + 1) * (nb + 1) + j + 1] + 1;
			else {
				int x = dp[(i + 1) * (nb + 1) + j];
				int y = dp[i * (nb + 1) + j + 1];

				dp[i * (nb + 1) + j] = x > y ? x : y;
			}
		}
	}

	struct diff_op *ops = calloc(na + nb + 1, sizeof(*ops));

	if (!ops) {
		free(dp);
		ret = brk_cmd_fail("out of memory");
		goto out;
	}

	size_t oi = 0;
	size_t i = 0, j = 0;

	while (i < na || j < nb) {
		if (i < na && j < nb && !strcmp(a[i], b[j])) {
			ops[oi++] = (struct diff_op){ ' ', (int)i, (int)j };
			i++;
			j++;
		} else if (j < nb &&
			   (i == na || dp[(i + 1) * (nb + 1) + j] >=
					       dp[i * (nb + 1) + j + 1])) {
			ops[oi++] = (struct diff_op){ '+', (int)i, (int)j };
			j++;
		} else {
			ops[oi++] = (struct diff_op){ '-', (int)i, (int)j };
			i++;
		}
	}

	printf("--- %s\n+++ %s\n", pa, pb);
	printf("@@ -1,%zu +1,%zu @@\n", na, nb);
	for (size_t k = 0; k < oi; k++) {
		if (ops[k].type == ' ')
			printf(" %s\n", a[ops[k].a]);
		else if (ops[k].type == '-')
			printf("-%s\n", a[ops[k].a]);
		else
			printf("+%s\n", b[ops[k].b]);
	}

	free(ops);
	free(dp);

out:
	for (size_t k = 0; k < na; k++)
		free(a[k]);
	for (size_t k = 0; k < nb; k++)
		free(b[k]);
	free(a);
	free(b);
	if (fa != stdin)
		fclose(fa);
	if (fb != stdin)
		fclose(fb);
	return ret;
}
