#include <brk/cmd.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char help[] =
	"Usage: xargs [-0] [-r] [-n N] [COMMAND [ARG...]]\n"
	"Run COMMAND with arguments read from standard input.\n"
	"\n"
	"Options:\n"
	"  -0       input items are separated by NUL\n"
	"  -r       do not run COMMAND if there is no input\n"
	"  -n N     use at most N input items per invocation\n"
	"  --help   display this help and exit";

struct token_list {
	char **items;
	size_t len;
	size_t cap;
};

static int token_add(struct token_list *tokens, char *token)
{
	if (tokens->len == tokens->cap) {
		size_t cap = tokens->cap ? tokens->cap * 2 : 32;
		char **items = realloc(tokens->items, cap * sizeof(*items));

		if (!items)
			return -1;
		tokens->items = items;
		tokens->cap = cap;
	}
	tokens->items[tokens->len++] = token;
	return 0;
}

static int read_all(int fd, char **out, size_t *out_len)
{
	size_t cap = 4096;
	size_t len = 0;
	char *buf = malloc(cap);

	if (!buf)
		return -1;

	for (;;) {
		if (len == cap) {
			if (cap > (size_t)-1 / 2) {
				free(buf);
				errno = ENOMEM;
				return -1;
			}
			cap *= 2;
			char *nb = realloc(buf, cap);

			if (!nb) {
				free(buf);
				return -1;
			}
			buf = nb;
		}

		ssize_t n = read(fd, buf + len, cap - len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			free(buf);
			return -1;
		}
		if (n == 0)
			break;
		len += (size_t)n;
	}

	if (len == cap) {
		char *nb = realloc(buf, cap + 1);

		if (!nb) {
			free(buf);
			return -1;
		}
		buf = nb;
	}
	buf[len] = '\0';
	*out = buf;
	*out_len = len;
	return 0;
}

static int split_tokens(char *buf, size_t len, bool nul_separated,
			struct token_list *tokens)
{
	size_t i = 0;

	while (i < len) {
		if (nul_separated) {
			while (i < len && buf[i] == '\0')
				i++;
			if (i == len)
				break;
			char *start = buf + i;

			while (i < len && buf[i] != '\0')
				i++;
			if (i < len)
				buf[i++] = '\0';
			if (token_add(tokens, start))
				return -1;
		} else {
			while (i < len && isspace((unsigned char)buf[i]))
				i++;
			if (i == len)
				break;
			char *start = buf + i;

			while (i < len && !isspace((unsigned char)buf[i]))
				i++;
			if (i < len)
				buf[i++] = '\0';
			if (token_add(tokens, start))
				return -1;
		}
	}
	return 0;
}

static int run_batch(char *const *prefix, int prefix_count, char *const *tokens,
		     size_t token_count)
{
	size_t argc = (size_t)prefix_count + token_count;
	char **args = malloc((argc + 1) * sizeof(*args));

	if (!args)
		return BRK_CMD_EXIT_FAIL;

	for (int i = 0; i < prefix_count; i++)
		args[i] = prefix[i];
	for (size_t i = 0; i < token_count; i++)
		args[prefix_count + i] = tokens[i];
	args[argc] = NULL;

	pid_t pid = fork();

	if (pid < 0) {
		perror("xargs: fork");
		free(args);
		return BRK_CMD_EXIT_FAIL;
	}
	if (pid == 0) {
		execvp(args[0], args);
		fprintf(stderr, "xargs: %s: %s\n", args[0], strerror(errno));
		_exit(127);
	}

	free(args);

	int status = 0;

	if (waitpid(pid, &status, 0) < 0) {
		perror("xargs: waitpid");
		return BRK_CMD_EXIT_FAIL;
	}
	if (!WIFEXITED(status))
		return 125;

	int code = WEXITSTATUS(status);

	if (code == 255)
		return 124;
	if (code != 0)
		return 123;
	return 0;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	struct token_list tokens = { 0 };
	char *input = NULL;
	size_t input_len = 0;
	unsigned long max_args = 0;
	bool nul_separated = false;
	bool no_run_empty = false;
	int opt;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	static const struct brk_cmd_option opts[] = {
		{ '0', '0', NULL, BRK_CMD_OPT_NO_ARG },
		{ 'r', 'r', NULL, BRK_CMD_OPT_NO_ARG },
		{ 'n', 'n', NULL, BRK_CMD_OPT_REQUIRED_ARG },
		{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
		BRK_CMD_OPTION_END,
	};

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(help);
		if (opt == '0')
			nul_separated = true;
		if (opt == 'r')
			no_run_empty = true;
		if (opt == 'n') {
			if (brk_cmd_parse_ulong(ctx.arg, &max_args, 1,
						1000000) != 0)
				return BRK_CMD_EXIT_FAIL;
		}
	}

	if (read_all(STDIN_FILENO, &input, &input_len) != 0)
		return brk_cmd_fail_errno("read");
	if (split_tokens(input, input_len, nul_separated, &tokens) != 0) {
		free(input);
		free(tokens.items);
		return brk_cmd_fail("out of memory");
	}

	char *default_command[] = { "echo", NULL };
	char **prefix = &argv[ctx.index];
	int prefix_count = brk_cmd_operand_count(&ctx);

	if (prefix_count == 0) {
		prefix = default_command;
		prefix_count = 1;
	}

	int ret = BRK_CMD_EXIT_OK;

	if (tokens.len == 0) {
		if (!no_run_empty)
			ret = run_batch(prefix, prefix_count, NULL, 0);
	} else {
		size_t start = 0;

		while (start < tokens.len) {
			size_t count = max_args ? (size_t)max_args :
						  tokens.len - start;

			if (count > tokens.len - start)
				count = tokens.len - start;

			int r = run_batch(prefix, prefix_count,
					  &tokens.items[start], count);

			if (r == 124 || (r != 0 && ret == 0))
				ret = r;
			start += count;
			if (!max_args)
				break;
		}
	}

	free(tokens.items);
	free(input);
	return ret;
}
