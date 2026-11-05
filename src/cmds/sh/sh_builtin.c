#include "sh.h"

char cwd[1024];

void sh_builtin_exit(struct sh_pipeline *p)
{
	struct sh_simple_cmd *c = &p->cmds[0];
	int code = 0;

	if (c->argv[1]) {
		char *end = NULL;
		long v = strtol(c->argv[1], &end, 10);

		if (end == c->argv[1] || *end != '\0' || v < 0 || v > 255) {
			fprintf(stderr,
				"sh: exit: numeric argument required\n");
			code = 2;
		} else {
			code = (int)v;
		}
	}
	sh_pipeline_release(p);
	exit(code);
}

sh_err_t sh_builtin_cd(struct sh_pipeline *p)
{
	struct sh_simple_cmd *c = &p->cmds[0];

	if (c->argc > 1) {
		if (chdir(c->argv[1]) < 0) {
			perror("sh: cd");
			return SH_ERR_IO;
		}
		if (!getcwd(cwd, sizeof(cwd) - 1)) {
			perror("sh: getcwd");
			return SH_ERR_IO;
		}
		cwd[sizeof(cwd) - 1] = '\0';
	}
	return SH_OK;
}

void sh_builtin_pwd(void)
{
	puts(cwd);
}

struct sh_alias {
	char *name;
	char *value;
	struct sh_alias *next;
};

static struct sh_alias *sh_aliases;

static void sh_alias_free_words(char **words, size_t count)
{
	for (size_t i = 0; i < count; i++)
		free(words[i]);
	free(words);
}

static int sh_alias_split(const char *value, char ***out, size_t *out_count)
{
	struct sh_lexer lex;
	struct sh_token token;
	char **words = NULL;
	size_t count = 0;
	size_t cap = 0;

	sh_lexer_init(&lex, value, strlen(value));
	for (;;) {
		sh_err_t err = sh_lexer_next(&lex, &token);

		if (err != SH_OK) {
			sh_alias_free_words(words, count);
			return -1;
		}
		if (token.type == SH_TOKEN_EOF)
			break;
		if (token.type != SH_TOKEN_WORD) {
			sh_token_release(&token);
			sh_alias_free_words(words, count);
			return -1;
		}

		if (count == cap) {
			size_t ncap = cap ? cap * 2 : 8;
			char **nwords = realloc(words, ncap * sizeof(*nwords));

			if (!nwords) {
				sh_token_release(&token);
				sh_alias_free_words(words, count);
				return -1;
			}
			words = nwords;
			cap = ncap;
		}
		words[count++] = token.text;
	}

	if (count == 0) {
		free(words);
		return -1;
	}
	*out = words;
	*out_count = count;
	return 0;
}

static struct sh_alias *sh_alias_find(const char *name)
{
	struct sh_alias *alias;

	for (alias = sh_aliases; alias; alias = alias->next)
		if (!strcmp(alias->name, name))
			return alias;
	return NULL;
}

static int sh_valid_alias_name(const char *name)
{
	if (!name[0])
		return 0;
	for (; *name; name++) {
		if (isspace((unsigned char)*name) ||
		    strchr("=|<>\'\"\\#", *name))
			return 0;
	}
	return 1;
}

static int sh_valid_env_name(const char *name)
{
	if (!isalpha((unsigned char)name[0]) && name[0] != '_')
		return 0;
	for (name++; *name; name++)
		if (!isalnum((unsigned char)*name) && *name != '_')
			return 0;
	return 1;
}

static int sh_alias_set(const char *name, const char *value)
{
	char **words;
	size_t count;
	struct sh_alias *alias;
	char *copy;

	if (!sh_valid_alias_name(name))
		return -1;
	if (sh_alias_split(value, &words, &count) != 0)
		return -1;
	sh_alias_free_words(words, count);

	copy = strdup(value);
	if (!copy)
		return -1;

	alias = sh_alias_find(name);
	if (alias) {
		free(alias->value);
		alias->value = copy;
		return 0;
	}

	alias = malloc(sizeof(*alias));
	if (!alias) {
		free(copy);
		return -1;
	}
	alias->name = strdup(name);
	if (!alias->name) {
		free(copy);
		free(alias);
		return -1;
	}
	alias->value = copy;
	alias->next = sh_aliases;
	sh_aliases = alias;
	return 0;
}

static void sh_alias_print(const struct sh_alias *alias)
{
	printf("alias %s='", alias->name);
	for (const char *p = alias->value; *p; p++) {
		if (*p == '\'')
			fputs("'\\''", stdout);
		else
			putchar(*p);
	}
	puts("'");
}

int sh_alias_expand_cmd(struct sh_simple_cmd *cmd)
{
	int depth = 0;

	while (cmd->argc > 0 && cmd->argv[0]) {
		struct sh_alias *alias = sh_alias_find(cmd->argv[0]);
		char **words;
		size_t count;
		size_t old_argc;
		char **old_argv;
		char **new_argv;
		int stop;

		if (!alias)
			return 0;
		if (++depth > 16) {
			fprintf(stderr, "sh: alias: expansion too deep\n");
			return -1;
		}
		if (sh_alias_split(alias->value, &words, &count) != 0)
			return -1;

		stop = !strcmp(words[0], alias->name);
		old_argc = cmd->argc;
		old_argv = cmd->argv;
		new_argv = malloc((count + old_argc) * sizeof(*new_argv));
		if (!new_argv) {
			sh_alias_free_words(words, count);
			return -1;
		}

		for (size_t i = 0; i < count; i++)
			new_argv[i] = words[i];
		for (size_t i = 1; i < old_argc; i++)
			new_argv[count + i - 1] = old_argv[i];
		new_argv[count + old_argc - 1] = NULL;

		free(words);
		free(old_argv[0]);
		free(old_argv);
		cmd->argv = new_argv;
		cmd->argc = count + old_argc - 1;

		if (stop)
			return 0;
	}
	return 0;
}

int sh_builtin_alias(struct sh_pipeline *p)
{
	struct sh_simple_cmd *cmd = &p->cmds[0];
	int status = 0;

	if (cmd->argc == 1) {
		for (struct sh_alias *alias = sh_aliases; alias;
		     alias = alias->next)
			sh_alias_print(alias);
		return 0;
	}

	for (size_t i = 1; i < cmd->argc; i++) {
		char *arg = cmd->argv[i];
		char *eq = strchr(arg, '=');

		if (eq) {
			*eq = '\0';
			if (sh_alias_set(arg, eq + 1) != 0) {
				fprintf(stderr,
					"sh: alias: invalid alias '%s'\n", arg);
				status = 1;
			}
			*eq = '=';
		} else {
			struct sh_alias *alias = sh_alias_find(arg);

			if (!alias) {
				fprintf(stderr, "sh: alias: %s: not found\n",
					arg);
				status = 1;
			} else {
				sh_alias_print(alias);
			}
		}
	}
	return status;
}

int sh_builtin_export(struct sh_pipeline *p)
{
	struct sh_simple_cmd *cmd = &p->cmds[0];
	int status = 0;

	if (cmd->argc == 1) {
		for (char **entry = environ; entry && *entry; entry++)
			puts(*entry);
		return 0;
	}

	for (size_t i = 1; i < cmd->argc; i++) {
		char *arg = cmd->argv[i];
		char *eq = strchr(arg, '=');

		if (eq) {
			*eq = '\0';
			if (!sh_valid_env_name(arg)) {
				fprintf(stderr,
					"sh: export: invalid name '%s'\n", arg);
				status = 1;
			} else if (setenv(arg, eq + 1, 1) != 0) {
				perror("sh: export");
				status = 1;
			}
			*eq = '=';
		} else {
			if (!sh_valid_env_name(arg)) {
				fprintf(stderr,
					"sh: export: invalid name '%s'\n", arg);
				status = 1;
			} else if (!getenv(arg) && setenv(arg, "", 1) != 0) {
				perror("sh: export");
				status = 1;
			}
		}
	}
	return status;
}
