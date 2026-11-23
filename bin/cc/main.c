#include "brkcc.h"

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

static void compile_source(const char *path, const char *asm_path)
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

	if (asm_path)
		asm_print_unit(asm_path);
	finalize_unit();
	unit_id++;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s [-c] [-E] [-S] [-o output] [-I dir] [-L dir] "
		"[-D name[=value]] [-U name] [-l name] [--cco2-object] "
		"input.c|input.o [...]\n",
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

static char *assembly_path(const char *input)
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
	strcat(p, ".s");
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

static char **lib_dirs;
static int nlib_dirs;
static char **lib_names;
static int nlib_names;

static bool global_defined(const char *name)
{
	struct unit *u;

	for (u = units; u; u = u->next) {
		struct sym *s;

		for (s = u->syms; s; s = s->next) {
			if (s->defined && !s->is_static && s->name &&
			    !strcmp(s->name, name))
				return true;
		}
	}
	return false;
}

static char *library_path(const char *dir, const char *name)
{
	size_t n = strlen(dir) + strlen(name) + 6;
	char *path = xmalloc(n);

	strcpy(path, dir);
	strcat(path, "/lib");
	strcat(path, name);
	strcat(path, ".a");
	return path;
}

static char *find_library(const char *name)
{
	static const char *const defaults[] = {
		"/usr/lib",
		"/usr/local/lib",
		"/lib",
	};
	char *path;
	int i;
	int j;

	for (j = 0; j < nlib_dirs; j++) {
		path = library_path(lib_dirs[j], name);
		if (open_archive(path))
			return path;
		free(path);
	}
	for (i = 0; i < (int)(sizeof(defaults) / sizeof(defaults[0])); i++) {
		path = library_path(defaults[i], name);
		if (open_archive(path))
			return path;
		free(path);
	}
	fatal("cannot find -l%s", name);
	return NULL;
}

static void resolve_libraries(void)
{
	struct archive **ars;
	bool progress;
	int i;

	if (nlib_names == 0)
		return;
	ars = xcalloc((size_t)nlib_names, sizeof(*ars));
	for (i = 0; i < nlib_names; i++) {
		char *path = find_library(lib_names[i]);

		ars[i] = open_archive(path);
		free(path);
	}

	/*
	 * Prefer the conventional _start when a library provides one: this is
	 * what makes libc's exit()/stdio cleanup run.  A program that defines
	 * its own _start keeps it, and freestanding links fall back to the
	 * compiler-generated __cc_start.
	 */
	if (!global_defined("_start")) {
		for (i = 0; i < nlib_names; i++) {
			if (archive_load_symbol(ars[i], "_start"))
				break;
		}
	}

	progress = true;
	while (progress) {
		struct unit *u;

		progress = false;
		for (u = units; u; u = u->next) {
			struct sym *s;

			for (s = u->syms; s; s = s->next) {
				if (s->defined || s->is_static ||
				    global_defined(s->name))
					continue;
				for (i = 0; i < nlib_names; i++) {
					if (archive_load_symbol(ars[i],
								s->name)) {
						progress = true;
						goto next_pass;
					}
				}
			}
		}
next_pass:
		;
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
	bool assembly_only = false;
	bool cco2_object = false;
	bool force_freestanding = false;
	bool force_hosted = false;
	bool out_given = false;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-o")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			out = argv[++i];
			out_given = true;
		} else if (!strcmp(argv[i], "-c")) {
			compile_only = true;
		} else if (!strcmp(argv[i], "--cco2-object")) {
			cco2_object = true;
		} else if (!strcmp(argv[i], "-ffreestanding")) {
			force_freestanding = true;
		} else if (!strcmp(argv[i], "-fhosted")) {
			force_hosted = true;
		} else if (!strcmp(argv[i], "-S")) {
			assembly_only = true;
		} else if (!strcmp(argv[i], "-I")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			pp_add_inc_path(argv[++i]);
		} else if (!strncmp(argv[i], "-I", 2) && argv[i][2]) {
			pp_add_inc_path(argv[i] + 2);
		} else if (!strcmp(argv[i], "-L")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			lib_dirs = xrealloc(lib_dirs,
					    (size_t)(nlib_dirs + 1) *
						    sizeof(*lib_dirs));
			lib_dirs[nlib_dirs++] = xstrdup(argv[++i]);
		} else if (!strncmp(argv[i], "-L", 2) && argv[i][2]) {
			lib_dirs = xrealloc(lib_dirs,
					    (size_t)(nlib_dirs + 1) *
						    sizeof(*lib_dirs));
			lib_dirs[nlib_dirs++] = xstrdup(argv[i] + 2);
		} else if (!strcmp(argv[i], "-l")) {
			if (i + 1 >= argc)
				usage(argv[0]);
			lib_names = xrealloc(lib_names,
					     (size_t)(nlib_names + 1) *
						     sizeof(*lib_names));
			lib_names[nlib_names++] = xstrdup(argv[++i]);
		} else if (!strncmp(argv[i], "-l", 2) && argv[i][2]) {
			lib_names = xrealloc(lib_names,
					     (size_t)(nlib_names + 1) *
						     sizeof(*lib_names));
			lib_names[nlib_names++] = xstrdup(argv[i] + 2);
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
	if (assembly_only && preprocess_only)
		fatal("-S and -E are mutually exclusive");
	if (assembly_only && compile_only)
		fatal("-S and -c are mutually exclusive");
	if ((compile_only || assembly_only) && ninputs > 1 && out_given)
		fatal("-o with %s requires a single input file",
		      compile_only ? "-c" : "-S");

	pp_add_inc_path(".");
	pp_add_inc_path("/usr/include");
	pp_add_inc_path("/usr/local/include");

	init_types();
	buf_init(&code);

	{
		bool hosted = force_hosted;
		bool has_libc = false;
		bool has_libgcc = false;
		int li;

		for (li = 0; li < nlib_names; li++) {
			if (!strcmp(lib_names[li], "c") ||
			    !strcmp(lib_names[li], "libc"))
				has_libc = true;
			if (!strcmp(lib_names[li], "gcc") ||
			    !strcmp(lib_names[li], "libgcc"))
				has_libgcc = true;
		}
		if (has_libc)
			hosted = true;
		if (force_freestanding)
			hosted = false;
		set_hosted_mode(hosted);

		/* The GNU driver links libgcc implicitly for hosted programs. */
		if (has_libc && !has_libgcc) {
			lib_names = xrealloc(lib_names,
					     (size_t)(nlib_names + 1) *
						     sizeof(*lib_names));
			lib_names[nlib_names++] = xstrdup("gcc");
		}
	}

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

	if (assembly_only) {
		for (i = 0; i < ninputs; i++) {
			char *asm_path;

			if (ends_with(inputs[i], ".o"))
				fatal("cannot use -S with object file '%s'",
				      inputs[i]);
			asm_path = (out_given && ninputs == 1) ?
					   (char *)out :
					   assembly_path(inputs[i]);
			compile_source(inputs[i], asm_path);
			if (asm_path != out)
				free(asm_path);
		}
		return 0;
	}

	if (compile_only) {
		for (i = 0; i < ninputs; i++) {
			char *obj;

			if (ends_with(inputs[i], ".o"))
				fatal("cannot use -c with object file '%s'",
				      inputs[i]);
			compile_source(inputs[i], NULL);
			obj = (out_given && ninputs == 1) ?
				      (char *)out :
				      object_path(inputs[i]);
			if (cco2_object)
				write_object(obj, unit_tail);
			else
				write_elf_object(obj, unit_tail);
			if (obj != out)
				free(obj);
		}
		return 0;
	}

	for (i = 0; i < ninputs; i++) {
		if (ends_with(inputs[i], ".o"))
			(void)read_object(inputs[i]);
		else
			compile_source(inputs[i], NULL);
	}

	resolve_libraries();
	link_and_write(out);
	return 0;
}
