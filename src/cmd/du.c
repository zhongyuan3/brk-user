#include <brk/cmd.h>
#include <brk/limits.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] =
	"Usage: du [-ask] [FILE...]\n"
	"Summarize disk usage of each FILE, recursively for directories.\n"
	"\n"
	"Options:\n"
	"  -a       write counts for all files, not just directories\n"
	"  -s       display only a total for each argument\n"
	"  -k       print sizes in 1024-byte blocks\n"
	"  --help   display this help and exit";

static const struct brk_cmd_option opts[] = {
	{ 'a', 'a', "all", BRK_CMD_OPT_NO_ARG },
	{ 's', 's', "summarize", BRK_CMD_OPT_NO_ARG },
	{ 'k', 'k', "kilobytes", BRK_CMD_OPT_NO_ARG },
	{ BRK_CMD_OPT_HELP, 0, "help", BRK_CMD_OPT_NO_ARG },
	BRK_CMD_OPTION_END,
};

struct du_opts {
	bool all;
	bool summarize;
	bool kib;
};

static unsigned long long display_blocks(unsigned long long blocks,
					 const struct du_opts *o)
{
	return o->kib ? (blocks + 1) / 2 : blocks;
}

static void print_usage_line(unsigned long long blocks, const char *path,
			     const struct du_opts *o)
{
	printf("%llu\t%s\n", display_blocks(blocks, o), path);
}

static int du_walk(const char *path, const struct du_opts *o,
		   unsigned long long *total_out)
{
	struct stat st;
	unsigned long long total;
	int ret = 0;

	if (lstat(path, &st) != 0) {
		brk_cmd_error("%s: %s", path, strerror(errno));
		return -1;
	}

	total = (unsigned long long)st.st_blocks;

	if (S_ISDIR(st.st_mode)) {
		int fd = open(path, O_RDONLY | O_DIRECTORY);

		if (fd < 0) {
			brk_cmd_error("%s: %s", path, strerror(errno));
			ret = -1;
		} else {
			char dbuf[4096];
			size_t used = 0;

			for (;;) {
				ssize_t n = posix_getdents(fd, dbuf + used,
							   sizeof(dbuf) - used,
							   0);

				if (n < 0) {
					if (errno == EINTR)
						continue;
					brk_cmd_error("%s: %s", path,
						      strerror(errno));
					ret = -1;
					break;
				}
				if (n == 0)
					break;
				used += (size_t)n;

				struct posix_dent *d =
					(struct posix_dent *)dbuf;
				size_t pos = 0;

				while (pos < used) {
					if (d->d_reclen == 0)
						break;
					if (strcmp(d->d_name, ".") &&
					    strcmp(d->d_name, "..")) {
						char child[PATH_MAX];
						unsigned long long sub = 0;
						int nlen = snprintf(
							child, sizeof(child),
							"%s/%s", path,
							d->d_name);

						if (nlen < 0 ||
						    (size_t)nlen >=
							    sizeof(child)) {
							brk_cmd_error(
								"%s: path too long",
								path);
							ret = -1;
						} else if (du_walk(child, o,
								   &sub) == 0) {
							total += sub;
						} else {
							ret = -1;
						}
					}
					pos += d->d_reclen;
					d = (struct posix_dent *)((char *)d +
								  d->d_reclen);
				}
				used = 0;
			}
			close(fd);
		}
	}

	*total_out = total;
	if (o->all && !o->summarize)
		print_usage_line(total, path, o);
	return ret;
}

int main(int argc, char *argv[])
{
	struct brk_cmd_optctx ctx;
	struct du_opts o = { 0 };
	int opt;
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);
	brk_cmd_optctx_init(&ctx, argc, argv);

	while ((opt = brk_cmd_optparse(&ctx, opts)) != 0) {
		if (opt == '?' || opt == ':')
			return BRK_CMD_EXIT_USAGE;
		if (opt == BRK_CMD_OPT_HELP)
			brk_cmd_help_exit(usage);
		if (opt == 'a')
			o.all = true;
		if (opt == 's')
			o.summarize = true;
		if (opt == 'k')
			o.kib = true;
	}

	if (o.summarize)
		o.all = false;

	if (brk_cmd_operand_count(&ctx) == 0) {
		unsigned long long total = 0;

		if (du_walk(".", &o, &total) != 0)
			return BRK_CMD_EXIT_FAIL;
		if (!o.all)
			print_usage_line(total, ".", &o);
		return BRK_CMD_EXIT_OK;
	}

	for (int i = 0; i < brk_cmd_operand_count(&ctx); i++) {
		const char *path = brk_cmd_operand(&ctx, i);
		unsigned long long total = 0;

		if (du_walk(path, &o, &total) != 0)
			ret = BRK_CMD_EXIT_FAIL;
		if (!o.all)
			print_usage_line(total, path, &o);
	}

	return ret;
}
