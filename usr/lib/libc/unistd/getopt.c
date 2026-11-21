#include <stdio.h>
#include <string.h>

#include <getopt.h>

/*
 * getopt() with the GNU long-option extension.
 *
 * Scanning stops at the first operand, as POSIX specifies (the GNU
 * argument permutation is not done).  A leading '+' in optstring is
 * accepted and ignored, a leading '-' makes operands themselves be
 * returned as if they were the argument to the option with value 1,
 * and a leading ':' silences the diagnostics and reports a missing
 * option argument as ':' instead of '?'.
 *
 * Long options match by their full name; a value may be attached
 * with '=' or taken from the next argv entry.
 */

char *optarg;
int optind = 1;
int opterr = 1;
int optopt;

static char *nextchar;		/* next letter of a short-option group */
static char *group_arg;		/* the argv entry that group belongs to */

static void diag(const char *argv0, const char *text)
{
	if (!opterr)
		return;
	if (!argv0 || !*argv0)
		argv0 = "getopt";
	fprintf(stderr, "%s: %s\n", argv0, text);
}

/* "invalid option -- 'c'" built around one letter of a group. */
static void diag_letter(const char *argv0, const char *text, char c)
{
	char msg[48];

	if (!opterr)
		return;
	snprintf(msg, sizeof(msg), "%s -- '%c'", text, c);
	diag(argv0, msg);
}

/*
 * One letter of a short-option group.  nextchar points at the letter
 * to process and is advanced past it.
 */
static int short_step(int argc, char *const argv[], const char *optstring,
		      int quiet)
{
	char c = *nextchar++;
	const char *spec;

	spec = strchr(optstring, c);
	if (c == ':' || !spec) {
		optopt = (unsigned char)c;
		diag_letter(argv[0], "invalid option", c);
		return '?';
	}

	if (spec[1] == ':') {
		if (spec[2] == ':') {
			/* Optional argument: only an attached value. */
			optarg = *nextchar ? nextchar : NULL;
		} else if (*nextchar) {
			optarg = nextchar;
		} else {
			if (optind + 1 >= argc) {
				optopt = (unsigned char)c;
				diag_letter(argv[0],
					    "option requires an argument", c);
				return quiet ? ':' : '?';
			}
			optarg = (char *)argv[++optind];
		}
		nextchar = NULL;
		optind++;	/* the option entry is fully consumed */
	} else {
		optarg = NULL;
		if (!*nextchar) {
			nextchar = NULL;
			optind++;
		}
	}
	return (unsigned char)c;
}

/*
 * Match a long option name (with an optional "=value" tail) against
 * longopts.  name points at the first letter after the dash(es).
 */
static int long_step(int argc, char *const argv[],
		     const struct option *longopts, int *longindex,
		     int quiet, const char *name)
{
	const char *eq = strchr(name, '=');
	size_t namelen = eq ? (size_t)(eq - name) : strlen(name);
	char msg[80];
	int i;

	for (i = 0; longopts[i].name; i++) {
		if (strlen(longopts[i].name) == namelen &&
		    !memcmp(longopts[i].name, name, namelen))
			break;
	}
	if (!longopts[i].name) {
		snprintf(msg, sizeof(msg), "unrecognized option '--%.*s'",
			 (int)namelen, name);
		diag(argv[0], msg);
		return '?';
	}

	if (longindex)
		*longindex = i;

	if (eq && longopts[i].has_arg == no_argument) {
		snprintf(msg, sizeof(msg), "option '--%s' takes no argument",
			 longopts[i].name);
		diag(argv[0], msg);
		return '?';
	}
	if (eq) {
		optarg = (char *)eq + 1;
		optind++;
	} else if (longopts[i].has_arg == required_argument) {
		if (optind + 1 >= argc) {
			snprintf(msg, sizeof(msg),
				 "option '--%s' requires an argument",
				 longopts[i].name);
			diag(argv[0], msg);
			optind++;
			return quiet ? ':' : '?';
		}
		optarg = (char *)argv[++optind];
		optind++;
	} else {
		optarg = NULL;
		optind++;
	}

	if (longopts[i].flag) {
		*longopts[i].flag = longopts[i].val;
		return 0;
	}
	return longopts[i].val;
}

static int getopt_core(int argc, char *const argv[], const char *optstring,
		       const struct option *longopts, int *longindex,
		       int long_only)
{
	const char *os = optstring;
	int quiet = 0;
	int pseudo = 0;

	if (os[0] == ':') {
		quiet = 1;
		os++;
	}
	if (os[0] == '+') {
		os++;
	} else if (os[0] == '-') {
		pseudo = 1;
		os++;
	}

	for (;;) {
		char *arg;

		/*
		 * Continue a short-option group only while optind still
		 * addresses the entry the group came from; a program
		 * resetting optind between scans must start fresh.
		 */
		if (nextchar && *nextchar && optind < argc &&
		    group_arg == argv[optind])
			return short_step(argc, argv, os, quiet);
		nextchar = NULL;

		if (optind >= argc)
			return -1;
		arg = (char *)argv[optind];

		if (arg[0] != '-' || !arg[1]) {
			/* Operand (or a lone "-"). */
			if (!pseudo)
				return -1;
			optarg = arg;
			optind++;
			return 1;
		}

		if (arg[1] == '-' && !arg[2]) {
			optind++;	/* "--" ends the option list */
			return -1;
		}

		if (arg[1] == '-') {
			if (!longopts) {
				nextchar = arg + 2;
				continue;
			}
			return long_step(argc, argv, longopts, longindex,
					 quiet, arg + 2);
		}

		if (long_only && longopts) {
			/* Maybe a long name written with one dash. */
			const char *eq = strchr(arg + 1, '=');
			size_t namelen = eq ? (size_t)(eq - arg - 1)
					    : strlen(arg + 1);
			int i;

			for (i = 0; longopts[i].name; i++) {
				if (strlen(longopts[i].name) == namelen &&
				    !memcmp(longopts[i].name, arg + 1,
					    namelen))
					break;
			}
			if (longopts[i].name) {
				return long_step(argc, argv, longopts,
						 longindex, quiet, arg + 1);
			}
		}

		nextchar = arg + 1;
		group_arg = arg;
	}
}

int getopt(int argc, char *const argv[], const char *optstring)
{
	return getopt_core(argc, argv, optstring, NULL, NULL, 0);
}

int getopt_long(int argc, char *const argv[], const char *optstring,
		const struct option *longopts, int *longindex)
{
	return getopt_core(argc, argv, optstring, longopts, longindex, 0);
}

int getopt_long_only(int argc, char *const argv[], const char *optstring,
		     const struct option *longopts, int *longindex)
{
	return getopt_core(argc, argv, optstring, longopts, longindex, 1);
}
