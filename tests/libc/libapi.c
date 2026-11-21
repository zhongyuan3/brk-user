/*
 * API tests for the Phase-1 libc additions: dirent streams, getopt,
 * the string extensions, rand determinism, div/atof, asctime/ctime/
 * timegm round trips and mkstemp/mkdtemp.  Like alloc.c this test
 * is linked against libc.a and runs inside BRK.
 *
 * Expected result: "libapi: ok"
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
static int fails;

static void check(int ok, const char *what)
{
	if (!ok) {
		fprintf(stderr, "libapi: %s\n", what);
		fails++;
	}
}

static void check_str(const char *got, const char *want, const char *what)
{
	if (strcmp(got, want)) {
		fprintf(stderr, "libapi: %s: got \"%s\" want \"%s\"\n", what,
			got, want);
		fails++;
	}
}

static void test_string(void)
{
	char buf[64];
	char *save;

	check_str(strcasestr("Hello, World!", "WORLD"), "World!",
		  "strcasestr hit");
	check(strcasestr("hello", "xyz") == NULL, "strcasestr miss");
	check(strcasestr("hello", "") != NULL, "strcasestr empty needle");

	strcpy(buf, " alpha,, beta ");
	save = NULL;
	/* strtok_r skips runs of delimiters, unlike strsep. */
	check_str(strtok_r(buf, " ,", &save), "alpha", "strtok_r 1");
	check_str(strtok_r(NULL, " ,", &save), "beta", "strtok_r 2");
	check(strtok_r(NULL, " ,", &save) == NULL, "strtok_r end");

	strcpy(buf, "one:two;three");
	save = buf;
	check_str(strsep(&save, ":;"), "one", "strsep 1");
	check_str(strsep(&save, ":;"), "two", "strsep 2");
	check_str(strsep(&save, ":;"), "three", "strsep 3");
	check(strsep(&save, ":;") == NULL, "strsep end");

	strcpy(buf, "name=value");
	check(strchrnul(buf, '=') == buf + 4, "strchrnul found");
	check(strchrnul(buf, 'z') == buf + 10, "strchrnul nul");

	check(ffs(0) == 0, "ffs(0)");
	check(ffs(1) == 1, "ffs(1)");
	check(ffs(0x80000000u) == 32, "ffs(high)");
	check(ffsll(0x100000000ll) == 33, "ffsll(2^32)");

	check_str(strsignal(SIGSEGV), "Segmentation fault", "strsignal");
	check_str(strsignal(999), "Signal 999", "strsignal out of range");

	strcpy(buf, "/usr/bin/cc");
	check_str(basename(buf), "cc", "basename");
	strcpy(buf, "/usr/");
	check_str(basename(buf), "usr", "basename trailing slash");
	strcpy(buf, "");
	check_str(basename(buf), ".", "basename empty");
	strcpy(buf, "/");
	check_str(basename(buf), "/", "basename root");

	strcpy(buf, "/usr/bin/cc");
	check_str(dirname(buf), "/usr/bin", "dirname");
	strcpy(buf, "cc");
	check_str(dirname(buf), ".", "dirname bare name");
	strcpy(buf, "/cc");
	check_str(dirname(buf), "/", "dirname root child");
}

static void test_stdlib(void)
{
	unsigned seed1 = 42, seed2 = 42;
	int r1, r2;

	srand(1);
	r1 = rand();
	srand(1);
	r2 = rand();
	check(r1 == r2, "rand determinism");
	check(r1 >= 0 && r1 <= RAND_MAX, "rand range");

	srand(7);
	r1 = rand();
	r2 = rand();
	check(r1 != r2, "rand advances");

	check(rand_r(&seed1) == rand_r(&seed2), "rand_r determinism");
	check(seed1 == seed2, "rand_r updates seed");

	{
		div_t d = div(-7, 3);
		ldiv_t l = ldiv(-7L, 3L);
		lldiv_t ll = lldiv(-7LL, 3LL);

		check(d.quot == -2 && d.rem == -1, "div");
		check(l.quot == -2 && l.rem == -1, "ldiv");
		check(ll.quot == -2 && ll.rem == -1, "lldiv");
	}

	check(atof("2.5") == 2.5, "atof");
	check(atof("-1e3") == -1000.0, "atof exp");
}

static void test_time(void)
{
	struct tm tm = { 0 };
	time_t t;
	char buf[32];

	tm.tm_year = 2026 - 1900;
	tm.tm_mon = 10;		/* November */
	tm.tm_mday = 21;
	tm.tm_hour = 12;
	tm.tm_min = 34;
	tm.tm_sec = 56;
	t = timegm(&tm);
	check(t == (time_t)1795264496, "timegm value");
	/* 2026-11-21 is a Saturday (6), day 325 of the year. */
	check(tm.tm_wday == 6, "timegm weekday");
	check(tm.tm_yday == 324, "timegm yday");

	gmtime_r(&t, &tm);
	check_str(asctime_r(&tm, buf), "Sat Nov 21 12:34:56 2026\n",
		  "asctime_r");
	check(ctime_r(&t, buf) != NULL && strlen(buf) == 25 &&
	      buf[24] == '\n', "ctime_r format");
	check(gmtime_r(&t, &tm) == &tm, "gmtime_r returns result");
	check(localtime_r(&t, &tm) != NULL, "localtime_r runs");
}

static void test_getopt(void)
{
	char *argv1[] = { "prog", "-a", "-bc", "operand", NULL };
	int argc1 = 4;
	int c;
	int saw_a = 0, saw_b = 0, saw_c = 0;

	optind = 1;
	while ((c = getopt(argc1, argv1, "abc")) != -1) {
		switch (c) {
		case 'a':
			saw_a = 1;
			break;
		case 'b':
			saw_b = 1;
			break;
		case 'c':
			saw_c = 1;
			break;
		default:
			saw_a = -1;
		}
	}
	check(saw_a == 1 && saw_b == 1 && saw_c == 1, "getopt cluster");
	check(optind == 3, "getopt stops at operand");

	{
		char *argv2[] = { "prog", "-ofile", "-v", NULL };
		int argc2 = 3;

		optind = 1;
		check(getopt(argc2, argv2, "o:v") == 'o', "getopt attached arg");
		check(optarg && !strcmp(optarg, "file"), "getopt attached value");
		check(getopt(argc2, argv2, "o:v") == 'v', "getopt no-arg option");
		check(optarg == NULL, "getopt no-arg optarg is NULL");
		check(getopt(argc2, argv2, "o:v") == -1, "getopt end");
	}

	{
		char *argv2[] = { "prog", "-q", NULL };
		int argc2 = 2;

		optind = 1;
		opterr = 0;
		check(getopt(argc2, argv2, "o:v") == '?', "getopt invalid");
		check(optopt == 'q', "getopt invalid optopt");
		opterr = 1;
	}

	{
		char *argv2[] = { "prog", "-o", NULL };
		int argc2 = 2;

		optind = 1;
		opterr = 0;
		check(getopt(argc2, argv2, "o:v") == '?', "getopt missing arg");
		opterr = 1;
	}

	{
		static struct option opts[] = {
			{ "verbose", no_argument, NULL, 'v' },
			{ "level", required_argument, NULL, 'l' },
			{ NULL, 0, NULL, 0 },
		};
		char *argv3[] = { "prog", "--verbose", "--level=9", "--",
				   "arg", NULL };
		int argc3 = 5;
		int idx;

		optind = 1;
		check(getopt_long(argc3, argv3, "", opts, &idx) == 'v',
		      "getopt_long flag");
		check(idx == 0, "getopt_long index");
		check(getopt_long(argc3, argv3, "", opts, &idx) == 'l',
		      "getopt_long attached");
		check(optarg && !strcmp(optarg, "9"), "getopt_long value");
		check(getopt_long(argc3, argv3, "", opts, &idx) == -1,
		      "getopt_long -- terminator");
		check(optind == 4 && !strcmp(argv3[4], "arg"),
		      "getopt_long operands after --");
	}
}

static void test_dirent_and_temp(void)
{
	DIR *d;
	struct dirent *de;
	struct dirent *first;
	long before = 0;
	char template[] = "/tmp/libapi.XXXXXX";
	char dirtemplate[] = "/tmp/libapid.XXXXXX";
	int fd;

	/* mkstemp: exclusive create, name filled in. */
	fd = mkstemp(template);
	check(fd >= 0, "mkstemp creates");
	if (fd >= 0) {
		char second[] = "/tmp/libapi.XXXXXX";
		int fd2;

		check(write(fd, "x", 1) == 1, "mkstemp writable");
		close(fd);

		/* A fresh template yields a distinct exclusive name. */
		fd2 = mkstemp(second);
		check(fd2 >= 0, "mkstemp second");
		if (fd2 >= 0) {
			check(strcmp(template, second) != 0,
			      "mkstemp distinct names");
			close(fd2);
			check(unlink(second) == 0, "mkstemp unlink second");
		}
		check(unlink(template) == 0, "mkstemp unlink");

		/* Without X letters the template is invalid. */
		check(mkstemp(template) < 0 && errno == EINVAL,
		      "mkstemp without X is EINVAL");
	}

	/* mkdtemp: directory appears with the filled name. */
	check(mkdtemp(dirtemplate) != NULL, "mkdtemp creates");
	check(rmdir(dirtemplate) == 0, "mkdtemp rmdir");

	/* opendir/readdir on /tmp, then rewind and re-walk. */
	d = opendir("/tmp");
	check(d != NULL, "opendir /tmp");
	if (d) {
		first = NULL;
		while ((de = readdir(d))) {
			if (!before)
				first = de;
			before++;
		}
		check(before > 0, "readdir lists entries");

		rewinddir(d);
		{
			long after = 0;

			while (readdir(d))
				after++;
			check(after == before, "rewinddir replays");
		}

		/* telldir/seekdr: cookies are stable entry indexes. */
		rewinddir(d);
		check(telldir(d) == 0, "telldir at start");
		de = readdir(d);
		check(de && telldir(d) == 1, "telldir advances");
		seekdir(d, 0);
		de = readdir(d);
		check(de && first && !strcmp(de->d_name, first->d_name),
		      "seekdir(0) restarts");
		seekdir(d, before - 1);
		de = readdir(d);
		check(de != NULL, "seekdir to last entry");
		de = readdir(d);
		check(de == NULL, "past the last entry is EOF");
		check(closedir(d) == 0, "closedir");
	}

	check(opendir("/no/such/dir") == NULL && errno == ENOENT,
	      "opendir missing");
	check(opendir("/etc/passwd") == NULL, "opendir on a file");
}

static void test_truncate(void)
{
	char template[] = "/tmp/libapit.XXXXXX";
	int fd = mkstemp(template);
	char buf[16];

	if (fd < 0) {
		check(0, "truncate test setup");
		return;
	}
	check(write(fd, "0123456789", 10) == 10, "truncate test write");
	check(ftruncate(fd, 4) == 0, "ftruncate");
	check(lseek(fd, 0, SEEK_END) == 4, "ftruncate size");
	close(fd);
	check(truncate(template, 2) == 0, "truncate");
	fd = open(template, O_RDONLY);
	check(fd >= 0 && read(fd, buf, sizeof(buf)) == 2 &&
	      !memcmp(buf, "01", 2), "truncate content");
	if (fd >= 0)
		close(fd);
	unlink(template);
}

static void test_unistd(void)
{
	char host[64];

	/* The in-system console is a tty; the test runs from it. */
	check(isatty(0) == 1, "isatty on console stdin");

	check(gethostname(host, sizeof(host)) == 0 && host[0],
	      "gethostname fills a name");
	check(gethostname(host, 0) == -1 && errno == EINVAL,
	      "gethostname rejects len 0");
}

int main(void)
{
	test_string();
	test_stdlib();
	test_time();
	test_getopt();
	test_dirent_and_temp();
	test_truncate();
	test_unistd();

	if (fails) {
		printf("libapi: %d failure(s)\n", fails);
		return 1;
	}
	printf("libapi: ok\n");
	return 0;
}
