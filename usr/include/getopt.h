#ifndef _GETOPT_H
#define _GETOPT_H

#include <unistd.h>

/*
 * GNU-style long options for getopt_long()/getopt_long_only().
 * flag != NULL makes the function store val through it and return 0;
 * otherwise the function returns val directly.
 */
struct option {
	const char *name;
	int has_arg;
	int *flag;
	int val;
};

#define no_argument 0
#define required_argument 1
#define optional_argument 2

int getopt_long(int argc, char *const argv[], const char *optstring,
		const struct option *longopts, int *longindex);
int getopt_long_only(int argc, char *const argv[], const char *optstring,
		     const struct option *longopts, int *longindex);

#endif
