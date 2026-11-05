#include "cc.h"

/* ------------------------------------------------------------------ */
/* top level                                                           */
/* ------------------------------------------------------------------ */

static char *read_source(const char *path, int *out_len)
{
	char *buf;

	if (try_read_file(path, &buf, out_len) < 0)
		fatal("cannot open '%s'", path);
	return buf;
}

static void compile_source(const char *path)
{
	char *src;
	char *pp;
	int len;
	int pplen;

	reset_emit_state();
	reset_parser_state();
	reset_pp_state();
	reset_codegen_state();
	reset_lexer_state();
	apply_cli_macros();

	src = read_source(path, &len);
	pp = preprocess(path, src, len, &pplen);
	free(src);
	lex_all(pp, pplen);
	free(pp);
	parse_translation_unit();

	/* Runtime is injected once by the linker; this unit only references it. */
	make_runtime_refs();

	for (struct obj *o = globals; o; o = o->next) {
		if (!o->is_function || !o->defined)
			continue;
		gen_function(o);
	}

	finalize_unit();
	unit_id++;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [-c] [-E] [-o output] [-I dir] [-D name[=value]] "
		"[-U name] input.c|input.o [...]\n",
		argv0);
}

static bool ends_with(const char *s, const char *suffix)
{
	size_t n = strlen(s);
	size_t m = strlen(suffix);

	return n >= m && !strcmp(s + n - m, suffix);
}

static char *object_path(const char *input)
{
	const char *base = strrchr(input, '/');
	size_t n;
	char *p;

	base = base ? base + 1 : input;
	n = strlen(base);
	p = xmalloc(n + 3);
	if (n > 2 && !strcmp(base + n - 2, ".c")) {
		memcpy(p, base, n - 2);
		p[n - 2] = '\0';
	} else {
		memcpy(p, base, n + 1);
	}
	strcat(p, ".o");
	return p;
}

static void write_stdout(const char *buf, int len)
{
	int off = 0;

	while (off < len) {
		ssize_t n = write(1, buf + off, (size_t)(len - off));

		if (n <= 0)
			fatal("write failed");
		off += (int)n;
	}
}

int main(int argc, char **argv)
{
	const char *out = "a.out";
	const char **inputs = NULL;
	int ninputs = 0;
	int i;
	bool preprocess_only = false;
	bool compile_only = false;
	bool out_given = false;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-o")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			out = argv[++i];
			out_given = true;
		} else if (!strcmp(argv[i], "-c")) {
			compile_only = true;
		} else if (!strcmp(argv[i], "-I")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			pp_add_inc_path(argv[++i]);
		} else if (!strncmp(argv[i], "-I", 2) && argv[i][2]) {
			pp_add_inc_path(argv[i] + 2);
		} else if (!strcmp(argv[i], "-D")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			record_cli_macro(false, argv[++i]);
		} else if (!strncmp(argv[i], "-D", 2) && argv[i][2]) {
			record_cli_macro(false, argv[i] + 2);
		} else if (!strcmp(argv[i], "-U")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			record_cli_macro(true, argv[++i]);
		} else if (!strncmp(argv[i], "-U", 2) && argv[i][2]) {
			record_cli_macro(true, argv[i] + 2);
		} else if (!strcmp(argv[i], "-E")) {
			preprocess_only = true;
		} else if (!strcmp(argv[i], "-h") ||
			   !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return 0;
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "cc: unknown option '%s'\n", argv[i]);
			usage(argv[0]);
			return 2;
		} else {
			inputs = xrealloc(inputs, (size_t)(ninputs + 1) *
							  sizeof(*inputs));
			inputs[ninputs++] = argv[i];
		}
	}
	if (ninputs == 0) {
		usage(argv[0]);
		return 2;
	}
	if (compile_only && preprocess_only)
		fatal("-c and -E are mutually exclusive");
	if (compile_only && ninputs > 1 && out_given)
		fatal("-o with -c requires a single input file");

	pp_add_inc_path(".");
	pp_add_inc_path("/usr/include");
	pp_add_inc_path("/usr/local/include");

	init_types();
	buf_init(&code);

	if (preprocess_only) {
		for (i = 0; i < ninputs; i++) {
			char *src;
			char *pp;
			int len;
			int pplen;

			apply_cli_macros();
			src = read_source(inputs[i], &len);
			pp = preprocess(inputs[i], src, len, &pplen);
			free(src);
			write_stdout(pp, pplen);
			free(pp);
		}
		return 0;
	}

	if (compile_only) {
		for (i = 0; i < ninputs; i++) {
			char *obj;

			if (ends_with(inputs[i], ".o"))
				fatal("cannot use -c with object file '%s'",
				      inputs[i]);
			compile_source(inputs[i]);
			obj = (out_given && ninputs == 1) ?
				      (char *)out :
				      object_path(inputs[i]);
			write_object(obj, unit_tail);
			if (obj != out)
				free(obj);
		}
		return 0;
	}

	for (i = 0; i < ninputs; i++) {
		if (ends_with(inputs[i], ".o"))
			(void)read_object(inputs[i]);
		else
			compile_source(inputs[i]);
	}

	link_and_write(out);
	return 0;
}
