#ifndef BRKTEST_HARNESS_H
#define BRKTEST_HARNESS_H

#include <stdbool.h>
#include <stddef.h>

struct test_ctx {
	unsigned int seed;
};

struct test_case {
	const char *name;
	int (*run)(struct test_ctx *ctx);
	unsigned int flags;
	unsigned int timeout_ms;
};

struct test_suite {
	const char *name;
	const char *description;
	const struct test_case *cases;
	size_t nr_cases;
};

#define TEST_REQUIRE_ROOT (1u << 0)
#define TEST_DESTRUCTIVE (1u << 1)
#define TEST_SLOW (1u << 2)
/* Run the case in its own process group so timeout can kill descendants. */
#define TEST_OWN_PGRP (1u << 3)

enum test_result {
	TEST_PASS = 0,
	TEST_FAIL = 1,
	TEST_SKIP = 2,
};

struct test_config {
	unsigned int seed;
	unsigned int timeout_ms;
	bool verbose;
	bool isolate;
	bool allow_destructive;
};

extern struct test_config test_config;

int test_run_case(const struct test_suite *suite, const struct test_case *tc,
		  struct test_ctx *ctx);
int test_run_suite(const struct test_suite *suite, struct test_ctx *ctx);

#endif
