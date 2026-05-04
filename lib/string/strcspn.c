#include <stdbool.h>
#include <string.h>

size_t strcspn(const char *s, const char *reject)
{
	bool map[256] = { false };
	for (int i = 0; reject[i] != '\0'; ++i)
		map[(int)reject[i]] = true;
	size_t cnt = 0;
	while (!map[(int)s[cnt]])
		++cnt;
	return cnt;
}
