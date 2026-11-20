#include <brk/cmd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] =
	"Usage: expr EXPRESSION\n"
	"Evaluate an expression.\n"
	"\n"
	"Operators: | & = != < <= > >= + - * / % : and parentheses.";

struct val {
	const char *s;
	long n;
	bool num;
};

struct parser {
	int argc;
	char **argv;
	int index;
	bool error;
};

static bool truth(const struct val *v)
{
	return v->num ? v->n != 0 : v->s && v->s[0];
}

static struct val parse_or(struct parser *p);

static struct val number(long n)
{
	return (struct val){ .n = n, .num = true };
}

static struct val string(const char *s)
{
	return (struct val){ .s = s, .num = false };
}

static struct val parse_primary(struct parser *p)
{
	if (p->index >= p->argc) {
		p->error = true;
		return number(0);
	}

	const char *tok = p->argv[p->index];

	if (!strcmp(tok, "(")) {
		p->index++;
		struct val v = parse_or(p);

		if (p->index >= p->argc || strcmp(p->argv[p->index], ")")) {
			p->error = true;
			return number(0);
		}
		p->index++;
		return v;
	}

	if (!strcmp(tok, "|") || !strcmp(tok, "&") || !strcmp(tok, "=") ||
	    !strcmp(tok, "!=") || !strcmp(tok, "<") || !strcmp(tok, "<=") ||
	    !strcmp(tok, ">") || !strcmp(tok, ">=") || !strcmp(tok, "+") ||
	    !strcmp(tok, "-") || !strcmp(tok, "*") || !strcmp(tok, "/") ||
	    !strcmp(tok, "%") || !strcmp(tok, ":") || !strcmp(tok, ")")) {
		p->error = true;
		return number(0);
	}

	p->index++;
	char *end;
	long n = strtol(tok, &end, 10);

	if (end != tok && *end == '\0')
		return number(n);
	return string(tok);
}

static struct val parse_mul(struct parser *p)
{
	struct val left = parse_primary(p);

	while (!p->error && p->index < p->argc) {
		const char *op = p->argv[p->index];
		struct val right;
		long a, b, r;

		if (strcmp(op, "*") && strcmp(op, "/") && strcmp(op, "%") &&
		    strcmp(op, ":"))
			break;
		p->index++;
		right = parse_primary(p);
		if (p->error)
			break;

		if (!strcmp(op, ":")) {
			if (left.num)
				left = number(0);
			else
				left = number(
					right.num || !right.s ?
						0 :
						(strncmp(left.s, right.s,
							 strlen(right.s)) == 0 ?
							 (long)strlen(right.s) :
							 0));
			continue;
		}

		if (!left.num || !right.num) {
			p->error = true;
			break;
		}
		a = left.n;
		b = right.n;
		if (!strcmp(op, "*"))
			r = a * b;
		else if (!strcmp(op, "/")) {
			if (!b) {
				p->error = true;
				break;
			}
			r = a / b;
		} else {
			if (!b) {
				p->error = true;
				break;
			}
			r = a % b;
		}
		left = number(r);
	}
	return left;
}

static struct val parse_add(struct parser *p)
{
	struct val left = parse_mul(p);

	while (!p->error && p->index < p->argc) {
		const char *op = p->argv[p->index];
		struct val right;

		if (strcmp(op, "+") && strcmp(op, "-"))
			break;
		p->index++;
		right = parse_mul(p);
		if (p->error)
			break;
		if (!left.num || !right.num) {
			p->error = true;
			break;
		}
		left = number(!strcmp(op, "+") ? left.n + right.n :
						 left.n - right.n);
	}
	return left;
}

static int compare_vals(const struct val *a, const struct val *b)
{
	if (a->num && b->num)
		return a->n < b->n ? -1 : a->n > b->n ? 1 : 0;

	long an = a->num ? a->n : strtol(a->s, NULL, 10);
	long bn = b->num ? b->n : strtol(b->s, NULL, 10);

	if (a->num || b->num)
		return an < bn ? -1 : an > bn ? 1 : 0;
	return strcmp(a->s, b->s);
}

static struct val parse_cmp(struct parser *p)
{
	struct val left = parse_add(p);

	if (p->error || p->index >= p->argc)
		return left;

	const char *op = p->argv[p->index];

	if (strcmp(op, "=") && strcmp(op, "!=") && strcmp(op, "<") &&
	    strcmp(op, "<=") && strcmp(op, ">") && strcmp(op, ">="))
		return left;

	p->index++;
	struct val right = parse_add(p);
	if (p->error)
		return left;

	int cmp = compare_vals(&left, &right);
	bool result;

	if (!strcmp(op, "="))
		result = cmp == 0;
	else if (!strcmp(op, "!="))
		result = cmp != 0;
	else if (!strcmp(op, "<"))
		result = cmp < 0;
	else if (!strcmp(op, "<="))
		result = cmp <= 0;
	else if (!strcmp(op, ">"))
		result = cmp > 0;
	else
		result = cmp >= 0;
	return number(result ? 1 : 0);
}

static struct val parse_and(struct parser *p)
{
	struct val left = parse_cmp(p);

	while (!p->error && p->index < p->argc &&
	       !strcmp(p->argv[p->index], "&")) {
		p->index++;
		struct val right = parse_cmp(p);

		if (p->error)
			break;
		left = truth(&left) && truth(&right) ? left : number(0);
	}
	return left;
}

static struct val parse_or(struct parser *p)
{
	struct val left = parse_and(p);

	while (!p->error && p->index < p->argc &&
	       !strcmp(p->argv[p->index], "|")) {
		p->index++;
		struct val right = parse_and(p);

		if (p->error)
			break;
		left = truth(&left) ? left : right;
	}
	return left;
}

int main(int argc, char *argv[])
{
	struct parser p = {
		.argc = argc - 1,
		.argv = &argv[1],
		.index = 0,
	};

	brk_cmd_init(argc, argv);
	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(usage);
	if (argc < 2)
		brk_cmd_usage(usage);

	struct val result = parse_or(&p);

	if (p.error || p.index != p.argc) {
		brk_cmd_error("invalid expression");
		return 2;
	}

	if (result.num)
		printf("%ld\n", result.n);
	else
		printf("%s\n", result.s ? result.s : "");
	return truth(&result) ? 0 : 1;
}
