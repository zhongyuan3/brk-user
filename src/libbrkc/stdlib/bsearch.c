#include <stdlib.h>

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
	      int (*compar)(const void *, const void *))
{
	void *try;
	int sign;
	while (nmemb > 0) {
		try = (char *)base + size * (nmemb / 2);
		sign = compar(key, try);
		if (sign < 0) {
			nmemb /= 2;
		} else if (sign > 0) {
			base = (char *)try + size;
			nmemb -= nmemb / 2 + 1;
		} else {
			return try;
		}
	}
	return NULL;
}
