/*
 * Shared-libc smoke test: printf, malloc, strcmp and qsort all come
 * from libc.so through DT_NEEDED; nothing libc is linked statically.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int main(void)
{
	const char *words[] = { "pear", "apple", "fig" };
	char *p = malloc(64);

	if (!p)
		return 1;
	strcpy(p, "shared libc");
	qsort(words, 3, sizeof(words[0]), cmp);
	if (strcmp(words[0], "apple") != 0)
		return 2;
	printf("libc dyn: %s %s %s %s\n", p, words[0], words[1],
	       words[2]);
	free(p);
	return 0;
}
