#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern const struct test_suite brktest_process_suite;
extern const struct test_suite brktest_signal_suite;
extern const struct test_suite brktest_fs_suite;
extern const struct test_suite brktest_creds_suite;
extern const struct test_suite brktest_posix_suite;
extern const struct test_suite brktest_net_suite;
extern const struct test_suite brktest_file_suite;
extern const struct test_suite brktest_io_suite;
extern const struct test_suite brktest_memory_suite;
extern const struct test_suite brktest_time_suite;
extern const struct test_suite brktest_sys_suite;
extern const struct test_suite brktest_dev_suite;

static const struct test_suite *const suites[] = {
	&brktest_process_suite, &brktest_signal_suite, &brktest_fs_suite,
	&brktest_creds_suite,	&brktest_posix_suite,  &brktest_net_suite,
	&brktest_file_suite,	&brktest_io_suite,     &brktest_memory_suite,
	&brktest_time_suite,	&brktest_sys_suite,    &brktest_dev_suite,
};

struct suite_alias {
	const char *name;
	const struct test_suite *suite;
	const char *case_name;
};

static const struct suite_alias aliases[] = {
	{ "fork", &brktest_process_suite, "fork" },
	{ "kill", &brktest_process_suite, "kill" },
	{ "sig", &brktest_signal_suite, "basic" },
	{ "meta", &brktest_fs_suite, "meta" },
	{ "perm", &brktest_creds_suite, "perm" },
	{ "group", &brktest_creds_suite, "group" },
	{ "auth", &brktest_creds_suite, "auth" },
	{ "symlink", &brktest_fs_suite, "symlink" },
	{ "posix", &brktest_posix_suite, "batch" },
	{ "net", &brktest_net_suite, "loopback" },
};

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [suite|suite.case|all] [options]\n\n"
		"Suites:\n"
		"  process  fork, wait, exit status and kill\n"
		"  signal   signal handlers, masks and SIGWINCH\n"
		"  fs       file metadata, links and symlinks\n"
		"  creds    uid/gid, permissions and auth databases\n"
		"  posix    POSIX syscall batch\n"
		"  net      TCP/UDP loopback sockets and inet helpers\n"
		"  file     regular file I/O, flags and directories\n"
		"  io       pipes, vectors, poll/select and SIGPIPE\n"
		"  memory   malloc, mmap, mprotect and copy-on-write\n"
		"  time     POSIX clocks, sleeps and wall-clock changes\n"
		"  sys      process identity, uname, rusage and /proc\n"
		"  dev      devtmpfs nodes and basic device access\n"
		"  all      run every suite (default)\n\n"
		"Legacy aliases:\n"
		"  fork kill sig meta perm group auth symlink posix net\n\n"
		"Options:\n"
		"  --list             list suites and cases\n"
		"  --seed N           random seed (default: pid)\n"
		"  --timeout MS       per-case timeout (default: 10000)\n"
		"  --no-isolate       run cases in the harness process\n"
		"  --allow-destructive allow wall-clock changes and similar tests\n"
		"  --verbose          verbose output\n"
		"  -h, --help         show this help\n",
		prog);
}

static const struct test_case *find_case(const struct test_suite *suite,
					 const char *name)
{
	for (size_t i = 0; i < suite->nr_cases; i++) {
		if (!strcmp(suite->cases[i].name, name))
			return &suite->cases[i];
	}
	return NULL;
}

static const struct test_suite *find_suite(const char *name)
{
	for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
		if (!strcmp(suites[i]->name, name))
			return suites[i];
	}
	return NULL;
}

static void list_tests(void)
{
	for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++) {
		const struct test_suite *suite = suites[i];

		printf("%s: %s\n", suite->name, suite->description);
		for (size_t j = 0; j < suite->nr_cases; j++)
			printf("  %s.%s\n", suite->name,
			       suite->cases[j].name);
	}
}

static int run_named_case(const struct test_suite *suite, const char *name,
			  struct test_ctx *ctx)
{
	const struct test_case *tc = find_case(suite, name);

	if (!tc) {
		fprintf(stderr, "brktest: no such case: %s.%s\n", suite->name,
			name);
		return -1;
	}
	return test_run_case(suite, tc, ctx) == TEST_FAIL ? -1 : 0;
}

static int run_alias(const struct suite_alias *alias, struct test_ctx *ctx)
{
	printf("\n=== %s ===\n", alias->name);
	int ret = run_named_case(alias->suite, alias->case_name, ctx);

	printf("=== %s: %s ===\n", alias->name, ret ? "FAILED" : "ok");
	return ret;
}

int main(int argc, char *argv[])
{
	const char *target = "all";
	struct test_ctx ctx;
	int failures = 0;
	int total = 0;

	if (argc > 1 && !strcmp(argv[1], "-check-euid"))
		return (getuid() == 1000 && geteuid() == 0) ? 0 : 1;
	if (argc > 1 && !strcmp(argv[1], "-check-egid"))
		return (getgid() == 1000 && getegid() == 2000) ? 0 : 1;

	test_config.seed = (unsigned int)getpid();

	bool list = false;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			usage(argv[0]);
			return 0;
		} else if (!strcmp(argv[i], "--list")) {
			list = true;
		} else if (!strcmp(argv[i], "--verbose")) {
			test_config.verbose = true;
		} else if (!strcmp(argv[i], "--no-isolate")) {
			test_config.isolate = false;
		} else if (!strcmp(argv[i], "--allow-destructive")) {
			test_config.allow_destructive = true;
		} else if (!strcmp(argv[i], "--seed") && i + 1 < argc) {
			test_config.seed = (unsigned int)strtoul(argv[++i], NULL,
								 0);
		} else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) {
			test_config.timeout_ms = (unsigned int)strtoul(
				argv[++i], NULL, 0);
		} else if (argv[i][0] == '-') {
			fprintf(stderr, "brktest: unknown option: %s\n\n",
				argv[i]);
			usage(argv[0]);
			return 1;
		} else {
			target = argv[i];
		}
	}

	if (list) {
		list_tests();
		return 0;
	}

	ctx.seed = test_config.seed;
	printf("brktest: seed=%u, timeout=%ums, isolate=%s\n", ctx.seed,
	       test_config.timeout_ms,
	       test_config.isolate ? "yes" : "no");

	if (!strcmp(target, "all")) {
		total = (int)(sizeof(suites) / sizeof(suites[0]));
		for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i++)
			if (test_run_suite(suites[i], &ctx) != 0)
				failures++;
	} else {
		for (size_t i = 0;
		     i < sizeof(aliases) / sizeof(aliases[0]); i++) {
			if (!strcmp(target, aliases[i].name)) {
				total = 1;
				if (run_alias(&aliases[i], &ctx) != 0)
					failures++;
				goto done;
			}
		}

		const struct test_suite *suite = find_suite(target);

		if (suite) {
			total = 1;
			if (test_run_suite(suite, &ctx) != 0)
				failures++;
			goto done;
		}

		const char *dot = strchr(target, '.');
		if (dot) {
			char name[64];
			size_t len = (size_t)(dot - target);

			if (len >= sizeof(name)) {
				fprintf(stderr,
					"brktest: unknown suite: %s\n\n",
					target);
				usage(argv[0]);
				return 1;
			}
			memcpy(name, target, len);
			name[len] = '\0';
			suite = find_suite(name);
			if (suite) {
				total = 1;
				if (run_named_case(suite, dot + 1, &ctx) != 0)
					failures++;
				goto done;
			}
		}

		fprintf(stderr, "brktest: unknown suite: %s\n\n", target);
		usage(argv[0]);
		return 1;
	}

done:
	printf("\n%d suite(s), %d failed\n", total, failures);
	return failures ? 1 : 0;
}
