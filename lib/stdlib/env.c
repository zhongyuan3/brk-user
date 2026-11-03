#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char **environ;

/*
 * environ initially points at the envp array supplied on the stack by the
 * kernel, so it must not be passed to realloc().  Copy it to the heap before
 * the first growth operation.
 */
static int env_owned;

static int env_ensure_owned(void)
{
	size_t count = 0;
	char **copy;

	if (env_owned)
		return 0;

	if (environ) {
		while (environ[count])
			count++;
	}

	copy = malloc((count + 1) * sizeof(*copy));
	if (!copy)
		return -1;

	for (size_t i = 0; i < count; i++)
		copy[i] = environ[i];
	copy[count] = NULL;

	environ = copy;
	env_owned = 1;
	return 0;
}

static int env_find(const char *name, size_t name_len)
{
	if (!environ)
		return -1;

	for (int i = 0; environ[i]; i++) {
		if (!strncmp(environ[i], name, name_len) &&
		    environ[i][name_len] == '=')
			return i;
	}
	return -1;
}

char *getenv(const char *name)
{
	size_t name_len;
	int index;

	if (!name || !name[0] || strchr(name, '='))
		return NULL;

	name_len = strlen(name);
	index = env_find(name, name_len);
	if (index < 0)
		return NULL;
	return environ[index] + name_len + 1;
}

int setenv(const char *name, const char *value, int overwrite)
{
	size_t name_len;
	size_t value_len;
	size_t count = 0;
	char *entry;
	char **new_environ;
	int index;

	if (!name || !name[0] || strchr(name, '=') || !value) {
		errno = EINVAL;
		return -1;
	}

	name_len = strlen(name);
	index = env_find(name, name_len);
	if (index >= 0 && !overwrite)
		return 0;

	value_len = strlen(value);
	entry = malloc(name_len + value_len + 2);
	if (!entry)
		return -1;
	memcpy(entry, name, name_len);
	entry[name_len] = '=';
	memcpy(entry + name_len + 1, value, value_len + 1);

	if (index >= 0) {
		environ[index] = entry;
		return 0;
	}

	if (env_ensure_owned() != 0) {
		free(entry);
		return -1;
	}

	while (environ[count])
		count++;

	new_environ = realloc(environ, (count + 2) * sizeof(*new_environ));
	if (!new_environ) {
		free(entry);
		return -1;
	}
	environ = new_environ;
	environ[count] = entry;
	environ[count + 1] = NULL;
	return 0;
}

int unsetenv(const char *name)
{
	size_t name_len;

	if (!name || !name[0] || strchr(name, '=')) {
		errno = EINVAL;
		return -1;
	}
	if (!environ)
		return 0;

	name_len = strlen(name);
	for (int i = 0; environ[i];) {
		if (!strncmp(environ[i], name, name_len) &&
		    environ[i][name_len] == '=') {
			int end = i;

			while (environ[end])
				end++;
			memmove(&environ[i], &environ[i + 1],
				(size_t)(end - i) * sizeof(*environ));
		} else {
			i++;
		}
	}
	return 0;
}
