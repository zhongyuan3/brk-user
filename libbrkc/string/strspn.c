#include <stdbool.h>
#include <string.h>

size_t strspn(const char *s, const char *accept)
{
	bool map[256] = { false };
	for (int i = 0; accept[i] != '\0'; ++i)
		map[(int)accept[i]] = true;
	size_t cnt = 0;
	while (map[(int)s[cnt]])
		++cnt;
	return cnt;
}
