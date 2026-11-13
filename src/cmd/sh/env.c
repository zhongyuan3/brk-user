#include "sh.h"

int sh_valid_env_name(const char *name)
{
	if (!isalpha((unsigned char)name[0]) && name[0] != '_')
		return 0;
	for (name++; *name; name++)
		if (!isalnum((unsigned char)*name) && *name != '_')
			return 0;
	return 1;
}

/*
 * Return true when word is a NAME=VALUE assignment with a portable name.
 * The string is restored before returning.
 */
static int split_assignment(char *word, char **value)
{
	char *eq = strchr(word, '=');
	char saved;
	int valid;

	if (!eq || eq == word)
		return 0;

	saved = *eq;
	*eq = '\0';
	valid = sh_valid_env_name(word);
	*eq = saved;
	if (!valid)
		return 0;

	*value = eq + 1;
	return 1;
}

int sh_cmd_extract_assignments(struct sh_simple_cmd *cmd)
{
	size_t count = 0;
	size_t remain;
	char **assign;

	while (count < cmd->argc) {
		char *value;

		if (!split_assignment(cmd->argv[count], &value))
			break;
		count++;
	}
	if (count == 0)
		return 0;

	assign = realloc(cmd->assign,
			 (cmd->nr_assign + count + 1) * sizeof(*assign));
	if (!assign)
		return -1;

	cmd->assign = assign;
	for (size_t i = 0; i < count; i++)
		cmd->assign[cmd->nr_assign + i] = cmd->argv[i];
	cmd->nr_assign += count;
	cmd->assign[cmd->nr_assign] = NULL;

	remain = cmd->argc - count;
	memmove(cmd->argv, cmd->argv + count, remain * sizeof(*cmd->argv));
	cmd->argc = remain;
	cmd->argv[remain] = NULL;
	return 0;
}

int sh_apply_assignments(const struct sh_simple_cmd *cmd)
{
	for (size_t i = 0; i < cmd->nr_assign; i++) {
		char *word = cmd->assign[i];
		char *eq = strchr(word, '=');
		char saved;
		int ret;

		if (!eq)
			continue;
		saved = *eq;
		*eq = '\0';
		ret = setenv(word, eq + 1, 1);
		*eq = saved;
		if (ret != 0)
			return -1;
	}
	return 0;
}

int sh_env_save_assignments(const struct sh_simple_cmd *cmd,
			    struct sh_env_save **out, size_t *nr)
{
	struct sh_env_save *saves;
	size_t i;

	*out = NULL;
	*nr = 0;
	if (cmd->nr_assign == 0)
		return 0;

	saves = calloc(cmd->nr_assign, sizeof(*saves));
	if (!saves)
		return -1;

	for (i = 0; i < cmd->nr_assign; i++) {
		char *word = cmd->assign[i];
		char *eq = strchr(word, '=');
		char saved;
		const char *old;

		if (!eq)
			goto fail;

		saved = *eq;
		*eq = '\0';
		saves[i].name = strdup(word);
		*eq = saved;
		if (!saves[i].name)
			goto fail;

		old = getenv(saves[i].name);
		if (old) {
			saves[i].value = strdup(old);
			if (!saves[i].value)
				goto fail;
		}
	}

	*out = saves;
	*nr = cmd->nr_assign;
	return 0;

fail:
	for (i = 0; i < cmd->nr_assign; i++) {
		free(saves[i].name);
		free(saves[i].value);
	}
	free(saves);
	return -1;
}

void sh_env_restore(struct sh_env_save *saves, size_t nr)
{
	if (!saves)
		return;

	for (size_t i = 0; i < nr; i++) {
		if (saves[i].value)
			(void)setenv(saves[i].name, saves[i].value, 1);
		else
			(void)unsetenv(saves[i].name);
		free(saves[i].name);
		free(saves[i].value);
	}
	free(saves);
}
