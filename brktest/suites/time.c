#include "../harness.h"
#include "../util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME 7
#endif

static long long ts_to_ns(const struct timespec *ts)
{
	return (long long)ts->tv_sec * 1000000000LL + ts->tv_nsec;
}

static int case_clock_basic(struct test_ctx *ctx)
{
	struct timespec real1, real2, mono1, mono2, boot, res;

	(void)ctx;
	if (clock_gettime(CLOCK_REALTIME, &real1) != 0 ||
	    clock_gettime(CLOCK_REALTIME, &real2) != 0)
		return test_fail_msg("time: CLOCK_REALTIME failed");
	if (clock_gettime(CLOCK_MONOTONIC, &mono1) != 0 ||
	    clock_gettime(CLOCK_MONOTONIC, &mono2) != 0)
		return test_fail_msg("time: CLOCK_MONOTONIC failed");
	if (clock_gettime(CLOCK_BOOTTIME, &boot) != 0)
		return test_fail_msg("time: CLOCK_BOOTTIME failed");
	if (ts_to_ns(&real2) < ts_to_ns(&real1))
		return test_fail_msg("time: realtime went backwards");
	if (ts_to_ns(&mono2) < ts_to_ns(&mono1))
		return test_fail_msg("time: monotonic went backwards");
	if (ts_to_ns(&boot) <= 0)
		return test_fail_msg("time: boottime is not positive");

	if (clock_getres(CLOCK_MONOTONIC, &res) != 0 || ts_to_ns(&res) <= 0)
		return test_fail_msg("time: monotonic resolution is invalid");
	if (clock_getres(CLOCK_PROCESS_CPUTIME_ID, &res) != 0 ||
	    ts_to_ns(&res) <= 0)
		return test_fail_msg("time: cpu resolution is invalid");
	if (clock_getres(CLOCK_BOOTTIME, &res) != 0 || ts_to_ns(&res) <= 0)
		return test_fail_msg("time: boottime resolution is invalid");
	return 0;
}

static int case_nanosleep(struct test_ctx *ctx)
{
	struct timespec req = { .tv_sec = 0, .tv_nsec = 10 * 1000 * 1000 };
	struct timespec start, end;

	(void)ctx;
	if (clock_gettime(CLOCK_MONOTONIC, &start) != 0)
		return test_fail_msg("time: clock_gettime failed");
	if (nanosleep(&req, NULL) != 0)
		return test_fail_msg("time: nanosleep failed: %d", errno);
	if (clock_gettime(CLOCK_MONOTONIC, &end) != 0)
		return test_fail_msg("time: clock_gettime failed");

	long long elapsed = ts_to_ns(&end) - ts_to_ns(&start);
	if (elapsed < 9 * 1000 * 1000LL || elapsed > 1000 * 1000 * 1000LL)
		return test_fail_msg("time: nanosleep elapsed=%lldns", elapsed);
	return 0;
}

static int case_clock_nanosleep_rel(struct test_ctx *ctx)
{
	struct timespec req = { .tv_sec = 0, .tv_nsec = 10 * 1000 * 1000 };
	struct timespec start, end;

	(void)ctx;
	if (clock_gettime(CLOCK_MONOTONIC, &start) != 0)
		return test_fail_msg("time: clock_gettime failed");
	if (clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL) != 0)
		return test_fail_msg("time: relative clock_nanosleep failed");
	if (clock_gettime(CLOCK_MONOTONIC, &end) != 0)
		return test_fail_msg("time: clock_gettime failed");

	long long elapsed = ts_to_ns(&end) - ts_to_ns(&start);
	if (elapsed < 9 * 1000 * 1000LL || elapsed > 1000 * 1000 * 1000LL)
		return test_fail_msg("time: relative sleep elapsed=%lldns",
				     elapsed);
	return 0;
}

static int case_clock_nanosleep_abs(struct test_ctx *ctx)
{
	struct timespec now, req, end;
	long long elapsed;

	(void)ctx;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return test_fail_msg("time: clock_gettime failed");
	req = now;
	req.tv_nsec += 10 * 1000 * 1000;
	if (req.tv_nsec >= 1000000000L) {
		req.tv_sec++;
		req.tv_nsec -= 1000000000L;
	}
	if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, NULL) != 0)
		return test_fail_msg("time: absolute clock_nanosleep failed");
	if (clock_gettime(CLOCK_MONOTONIC, &end) != 0)
		return test_fail_msg("time: clock_gettime failed");

	elapsed = ts_to_ns(&end) - ts_to_ns(&now);
	if (elapsed < 9 * 1000 * 1000LL || elapsed > 1000 * 1000 * 1000LL)
		return test_fail_msg("time: absolute sleep elapsed=%lldns",
				     elapsed);
	return 0;
}

static int case_wall_set(struct test_ctx *ctx)
{
	struct timeval before, target, after;
	struct timespec mono1, mono2;
	long long real_diff;
	long long mono_diff;

	(void)ctx;
	if (gettimeofday(&before, NULL) != 0)
		return test_fail_msg("time: gettimeofday failed");
	if (clock_gettime(CLOCK_MONOTONIC, &mono1) != 0)
		return test_fail_msg("time: clock_gettime failed");

	target = before;
	target.tv_sec += 2;
	if (settimeofday(&target, NULL) != 0)
		return test_fail_msg("time: settimeofday failed: %d", errno);
	if (gettimeofday(&after, NULL) != 0)
		return test_fail_msg("time: gettimeofday failed");
	if (clock_gettime(CLOCK_MONOTONIC, &mono2) != 0)
		return test_fail_msg("time: clock_gettime failed");

	real_diff = (long long)(after.tv_sec - before.tv_sec) * 1000000LL +
		    (after.tv_usec - before.tv_usec);
	mono_diff = ts_to_ns(&mono2) - ts_to_ns(&mono1);
	if (real_diff < 1900000 || real_diff > 3000000)
		return test_fail_msg("time: realtime delta is %lldus",
				     real_diff);
	if (mono_diff > 1000 * 1000 * 1000LL)
		return test_fail_msg("time: monotonic changed by %lldns",
				     mono_diff);

	if (settimeofday(&before, NULL) != 0)
		return test_fail_msg("time: restore settimeofday failed");
	return 0;
}

static int case_tz_offset(struct test_ctx *ctx)
{
	static const struct {
		const char *tz;
		long want;
	} cases[] = {
		{ NULL, 0 },
		{ "", 0 },
		{ "UTC", 0 },
		{ "GMT", 0 },
		{ "UTC+8", 8 * 3600 },
		{ "UTC-5", -5 * 3600 },
		{ "UTC+05:30", 5 * 3600 + 30 * 60 },
		{ "CST-8", 8 * 3600 },
		{ "EST5", -5 * 3600 },
		{ "Asia/Shanghai", 0 },
		{ "UTC+99", 0 },
	};
	const char *old = getenv("TZ");
	char *saved = old ? strdup(old) : NULL;

	(void)ctx;

	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		long got;

		if (cases[i].tz) {
			if (setenv("TZ", cases[i].tz, 1) != 0) {
				free(saved);
				return test_fail_msg("time: setenv TZ failed");
			}
		} else {
			(void)unsetenv("TZ");
		}

		got = brk_tz_offset();
		if (got != cases[i].want) {
			free(saved);
			return test_fail_msg("time: TZ=%s gave %ld, want %ld",
					     cases[i].tz ? cases[i].tz :
							   "(unset)",
					     got, cases[i].want);
		}
	}

	if (saved) {
		(void)setenv("TZ", saved, 1);
		free(saved);
	} else {
		(void)unsetenv("TZ");
	}
	return 0;
}

static const struct test_case time_cases[] = {
	{ "clocks", case_clock_basic, TEST_OWN_PGRP, 10000 },
	{ "nanosleep", case_nanosleep, TEST_OWN_PGRP, 10000 },
	{ "clock_nanosleep_rel", case_clock_nanosleep_rel, TEST_OWN_PGRP,
	  10000 },
	{ "clock_nanosleep_abs", case_clock_nanosleep_abs, TEST_OWN_PGRP,
	  10000 },
	{ "wall_set", case_wall_set,
	  TEST_REQUIRE_ROOT | TEST_DESTRUCTIVE | TEST_OWN_PGRP, 15000 },
	{ "tz", case_tz_offset, 0, 10000 },
};

const struct test_suite brktest_time_suite = {
	.name = "time",
	.description = "POSIX clocks, sleeps and wall-clock changes",
	.cases = time_cases,
	.nr_cases = sizeof(time_cases) / sizeof(time_cases[0]),
};
