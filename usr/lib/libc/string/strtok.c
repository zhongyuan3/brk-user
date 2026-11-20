#include <stdbool.h>
#include <string.h>

char *strtok(char *str, const char *delim)
{
	static char *saved_ptr = NULL;

	char *start;

	if (str) {
		saved_ptr = str;
	} else {
		if (!saved_ptr)
			return NULL;
	}

	start = saved_ptr;

	while (*start != '\0') {
		bool is_delim = false;
		const char *d = delim;
		while (*d != '\0') {
			if (*start == *d) {
				is_delim = true;
				break;
			}
			d++;
		}

		if (!is_delim)
			break;

		start++;
	}

	if (*start == '\0') {
		saved_ptr = NULL;
		return NULL;
	}

	char *end = start;
	while (*end != '\0') {
		bool is_delim = false;
		const char *d = delim;
		while (*d != '\0') {
			if (*end == *d) {
				is_delim = true;
				break;
			}
			d++;
		}

		if (is_delim)
			break;

		end++;
	}

	if (*end != '\0') {
		*end = '\0';
		saved_ptr = end + 1;
	} else {
		saved_ptr = NULL;
	}

	return start;
}
