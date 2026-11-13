#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char **environ;

/*
 * environ initially points at the envp array supplied on the stack by the
 * kernel.  env_heap is the heap copy we own.  Comparing environ against
 * env_heap, instead of using a single boolean, keeps setenv()/clearenv()
 * correct even if an application assigns environ directly.
 */
static char **env_heap;

static int env_ensure_owned(void)
{
	size_t count = 0;
	char **copy;

	if (environ && environ == env_heap)
		return 0;

	/*
	 * The application replaced environ behind our back.  The old array
	 * is ours, but its strings may have been copied into the foreign
	 * array, so only free the pointer array here.
	 */
	if (env_heap) {
		if (!environ) {
			for (size_t i = 0; env_heap[i]; i++)
				free(env_heap[i]);
		}
		free(env_heap);
		env_heap = NULL;
	}

	if (environ) {
		while (environ[count])
			count++;
	}

	copy = malloc((count + 1) * sizeof(*copy));
	if (!copy) {
		errno = ENOMEM;
		return -1;
	}

	for (size_t i = 0; i < count; i++) {
		copy[i] = strdup(environ[i]);
		if (!copy[i]) {
			while (i > 0)
				free(copy[--i]);
			free(copy);
			errno = ENOMEM;
			return -1;
		}
	}
	copy[count] = NULL;

	environ = copy;
	env_heap = copy;
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

static int env_set(const char *name, size_t name_len, const char *value,
		   int overwrite)
{
	size_t value_len;
	size_t count = 0;
	char *entry;
	char **new_environ;
	int index;

	index = env_find(name, name_len);
	if (index >= 0 && !overwrite)
		return 0;

	value_len = strlen(value);
	if (value_len > (size_t)-1 - name_len - 2) {
		errno = ENOMEM;
		return -1;
	}

	entry = malloc(name_len + value_len + 2);
	if (!entry) {
		errno = ENOMEM;
		return -1;
	}
	memcpy(entry, name, name_len);
	entry[name_len] = '=';
	memcpy(entry + name_len + 1, value, value_len + 1);

	if (index >= 0) {
		if (env_ensure_owned() != 0) {
			free(entry);
			return -1;
		}
		free(environ[index]);
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
		errno = ENOMEM;
		return -1;
	}
	environ = new_environ;
	env_heap = new_environ;
	environ[count] = entry;
	environ[count + 1] = NULL;
	return 0;
}

int setenv(const char *name, const char *value, int overwrite)
{
	if (!name || !name[0] || strchr(name, '=') || !value) {
		errno = EINVAL;
		return -1;
	}

	return env_set(name, strlen(name), value, overwrite);
}

/*
 * The string is copied into the environment.  This is stricter than some
 * historical implementations, but it keeps the owned environment consistent
 * and is safe for callers that pass stack or temporary buffers.
 */
int putenv(char *string)
{
	char *eq;
	char *name;
	size_t name_len;
	int ret;

	if (!string) {
		errno = EINVAL;
		return -1;
	}

	eq = strchr(string, '=');
	if (!eq || eq == string) {
		errno = EINVAL;
		return -1;
	}

	name_len = (size_t)(eq - string);
	name = malloc(name_len + 1);
	if (!name) {
		errno = ENOMEM;
		return -1;
	}
	memcpy(name, string, name_len);
	name[name_len] = '\0';

	ret = env_set(name, name_len, eq + 1, 1);
	free(name);
	return ret;
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
	if (env_ensure_owned() != 0)
		return -1;

	for (int i = 0; environ[i];) {
		if (!strncmp(environ[i], name, name_len) &&
		    environ[i][name_len] == '=') {
			int end = i;

			free(environ[i]);
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

int clearenv(void)
{
	if (env_heap) {
		if (!environ || environ == env_heap) {
			for (size_t i = 0; env_heap[i]; i++)
				free(env_heap[i]);
		}
		free(env_heap);
		env_heap = NULL;
	}
	environ = NULL;
	return 0;
}
