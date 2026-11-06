#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int put_escaped(const char *s)
{
	while (*s) {
		if (*s != '\\') {
			putchar(*s++);
			continue;
		}
		s++;
		switch (*s) {
		case 'a':
			putchar('\a');
			break;
		case 'b':
			putchar('\b');
			break;
		case 'c':
			return -1;
		case 'f':
			putchar('\f');
			break;
		case 'n':
			putchar('\n');
			break;
		case 'r':
			putchar('\r');
			break;
		case 't':
			putchar('\t');
			break;
		case 'v':
			putchar('\v');
			break;
		case '\\':
			putchar('\\');
			break;
		case '0':
			putchar('\0');
			break;
		case '\0':
			putchar('\\');
			return 0;
		default:
			putchar('\\');
			putchar(*s);
			break;
		}
		s++;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	bool newline = true;
	bool escapes = false;
	int i = 1;

	while (i < argc) {
		if (!strcmp(argv[i], "--")) {
			i++;
			break;
		}
		if (!strcmp(argv[i], "-n")) {
			newline = false;
			i++;
			continue;
		}
		if (!strcmp(argv[i], "-e")) {
			escapes = true;
			i++;
			continue;
		}
		if (!strcmp(argv[i], "-E")) {
			escapes = false;
			i++;
			continue;
		}
		break;
	}

	for (int j = i; j < argc; j++) {
		if (j > i)
			putchar(' ');
		if (escapes) {
			if (put_escaped(argv[j]) < 0)
				return 0;
		} else {
			fputs(argv[j], stdout);
		}
	}

	if (newline)
		putchar('\n');
	return 0;
}
