#include <brk/cmd.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] = "Usage: printf FORMAT [ARG...]\n"
			    "Format and print arguments according to FORMAT.";

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
static void print_fmt(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}
#pragma GCC diagnostic pop

static int emit_one_escape(const char **pp)
{
	char c = *(*pp)++;
	int value;

	switch (c) {
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
	case 'x':
		value = 0;
		for (int i = 0; i < 2; i++) {
			int d;

			if (**pp >= '0' && **pp <= '9')
				d = **pp - '0';
			else if (**pp >= 'a' && **pp <= 'f')
				d = **pp - 'a' + 10;
			else if (**pp >= 'A' && **pp <= 'F')
				d = **pp - 'A' + 10;
			else
				break;
			value = value * 16 + d;
			(*pp)++;
		}
		putchar(value);
		break;
	default:
		if (c >= '0' && c <= '7') {
			value = c - '0';
			for (int i = 1; i < 3; i++) {
				if (**pp < '0' || **pp > '7')
					break;
				value = value * 8 + (**pp - '0');
				(*pp)++;
			}
			putchar(value);
		} else {
			putchar(c);
		}
		break;
	}
	return 0;
}

static const char *next_arg(int argc, char *argv[], int *index)
{
	if (*index >= argc)
		return "";
	return argv[(*index)++];
}

int main(int argc, char *argv[])
{
	const char *fmt;
	const char *p;
	int argi = 2;

	brk_cmd_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(usage);
	if (argc < 2)
		brk_cmd_usage(usage);

	fmt = argv[1];
	for (p = fmt; *p;) {
		char spec[64];
		int si = 0;
		int len = 0;
		char conv;

		if (*p == '\\') {
			p++;
			if (!*p || emit_one_escape(&p) < 0)
				break;
			continue;
		}
		if (*p != '%') {
			putchar(*p++);
			continue;
		}

		p++;
		if (*p == '%') {
			putchar('%');
			p++;
			continue;
		}

		spec[si++] = '%';
		while (*p && strchr("-+ #0", *p) && si < (int)sizeof(spec) - 8)
			spec[si++] = *p++;
		while (*p >= '0' && *p <= '9' && si < (int)sizeof(spec) - 8)
			spec[si++] = *p++;
		if (*p == '.') {
			spec[si++] = *p++;
			while (*p >= '0' && *p <= '9' &&
			       si < (int)sizeof(spec) - 8)
				spec[si++] = *p++;
		}

		if (*p == 'h') {
			len = 1;
			spec[si++] = *p++;
			if (*p == 'h') {
				len = 2;
				spec[si++] = *p++;
			}
		} else if (*p == 'l') {
			len = 3;
			spec[si++] = *p++;
			if (*p == 'l') {
				len = 4;
				spec[si++] = *p++;
			}
		} else if (*p == 'z' || *p == 'j' || *p == 't') {
			len = 5;
			spec[si++] = *p++;
		}

		conv = *p;
		if (!conv)
			break;
		p++;

		const char *arg = next_arg(argc, argv, &argi);

		switch (conv) {
		case 'd':
		case 'i': {
			char *end;
			long long v;

			if (len == 4)
				v = strtoll(arg, &end, 0);
			else
				v = strtol(arg, &end, 0);
			if (*end) {
				brk_cmd_error("invalid number: %s", arg);
				return BRK_CMD_EXIT_FAIL;
			}
			if (len == 0)
				spec[si++] = 'l';
			spec[si++] = conv;
			spec[si] = '\0';
			if (len == 1 || len == 2)
				print_fmt(spec, (int)v);
			else if (len == 4)
				print_fmt(spec, v);
			else
				print_fmt(spec, (long)v);
			break;
		}
		case 'o':
		case 'u':
		case 'x':
		case 'X': {
			char *end;
			unsigned long long v;

			if (len == 4)
				v = strtoull(arg, &end, 0);
			else
				v = strtoul(arg, &end, 0);
			if (*end) {
				brk_cmd_error("invalid number: %s", arg);
				return BRK_CMD_EXIT_FAIL;
			}
			if (len == 0)
				spec[si++] = 'l';
			spec[si++] = conv;
			spec[si] = '\0';
			if (len == 1 || len == 2)
				print_fmt(spec, (unsigned int)v);
			else if (len == 4)
				print_fmt(spec, v);
			else
				print_fmt(spec, (unsigned long)v);
			break;
		}
		case 'c':
			spec[si++] = 'c';
			spec[si] = '\0';
			print_fmt(spec, (int)(unsigned char)arg[0]);
			break;
		case 's':
			spec[si++] = 's';
			spec[si] = '\0';
			print_fmt(spec, arg);
			break;
		case 'b': {
			const char *a = arg;

			while (*a) {
				if (*a != '\\') {
					putchar(*a++);
					continue;
				}
				a++;
				if (!*a || emit_one_escape(&a) < 0)
					break;
			}
			break;
		}
		default:
			putchar('%');
			putchar(conv);
			break;
		}
	}

	return BRK_CMD_EXIT_OK;
}
