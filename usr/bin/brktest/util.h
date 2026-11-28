#ifndef _UTIL_H
#define _UTIL_H

#include <stddef.h>

#define TEST_SKIP_CODE 77

int test_fail_msg(const char *fmt, ...);
int test_skip(const char *fmt, ...);
int test_tmpdir(char *buf, size_t size, const char *tag);

#endif
