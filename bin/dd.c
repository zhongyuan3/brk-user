#include <brk/cmd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char usage[] =
	"Usage: dd [if=FILE] [of=FILE] [bs=N] [count=N] [skip=N] [seek=N]\n"
	"          [conv=notrunc,sync] [status=none]\n"
	"Copy and convert raw data.";

struct dd_opts {
	const char *input;
	const char *output;
	size_t block_size;
	unsigned long long count;
	unsigned long long skip;
	unsigned long long seek;
	bool count_set;
	bool notrunc;
	bool sync;
	bool status;
};

static int parse_number(const char *s, unsigned long long *out)
{
	char *end;
	unsigned long long value;
	unsigned long long mult = 1;

	if (!s || !s[0])
		return -1;

	errno = 0;
	value = strtoull(s, &end, 0);
	if (errno || end == s)
		return -1;

	if (*end) {
		switch (*end) {
		case 'c':
			mult = 1;
			break;
		case 'w':
			mult = 2;
			break;
		case 'b':
			mult = 512;
			break;
		case 'k':
		case 'K':
			mult = 1024;
			break;
		case 'M':
			mult = 1024ULL * 1024;
			break;
		case 'G':
			mult = 1024ULL * 1024 * 1024;
			break;
		default:
			return -1;
		}
		end++;
		if (*end)
			return -1;
	}

	if (value > (unsigned long long)-1 / mult)
		return -1;
	*out = value * mult;
	return 0;
}

static int write_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;

	while (len) {
		ssize_t n = write(fd, p, len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0) {
			errno = EIO;
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

static int parse_conv(const char *s, struct dd_opts *o)
{
	char buf[128];

	if (strlen(s) >= sizeof(buf))
		return -1;
	strcpy(buf, s);

	char *tok = strtok(buf, ",");

	while (tok) {
		if (!strcmp(tok, "notrunc"))
			o->notrunc = true;
		else if (!strcmp(tok, "sync"))
			o->sync = true;
		else
			return -1;
		tok = strtok(NULL, ",");
	}
	return 0;
}

int main(int argc, char *argv[])
{
	struct dd_opts o = {
		.input = NULL,
		.output = NULL,
		.block_size = 512,
		.status = true,
	};
	int in_fd = STDIN_FILENO;
	int out_fd = STDOUT_FILENO;
	unsigned char *buf = NULL;
	unsigned long long in_records = 0;
	unsigned long long out_records = 0;
	int ret = BRK_CMD_EXIT_FAIL;

	brk_cmd_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(usage);

	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!strncmp(arg, "if=", 3))
			o.input = arg + 3;
		else if (!strncmp(arg, "of=", 3))
			o.output = arg + 3;
		else if (!strncmp(arg, "bs=", 3)) {
			unsigned long long n;

			if (parse_number(arg + 3, &n) != 0 || !n)
				brk_cmd_usage(usage);
			o.block_size = (size_t)n;
		} else if (!strncmp(arg, "count=", 6)) {
			if (parse_number(arg + 6, &o.count) != 0)
				brk_cmd_usage(usage);
			o.count_set = true;
		} else if (!strncmp(arg, "skip=", 5)) {
			if (parse_number(arg + 5, &o.skip) != 0)
				brk_cmd_usage(usage);
		} else if (!strncmp(arg, "seek=", 5)) {
			if (parse_number(arg + 5, &o.seek) != 0)
				brk_cmd_usage(usage);
		} else if (!strncmp(arg, "conv=", 5)) {
			if (parse_conv(arg + 5, &o) != 0)
				brk_cmd_usage(usage);
		} else if (!strcmp(arg, "status=none")) {
			o.status = false;
		} else {
			brk_cmd_usage(usage);
		}
	}

	if (o.input) {
		in_fd = open(o.input, O_RDONLY);
		if (in_fd < 0)
			return brk_cmd_fail("%s: %s", o.input, strerror(errno));
	}
	if (o.output) {
		int flags = O_WRONLY | O_CREAT;

		if (!o.notrunc)
			flags |= O_TRUNC;
		out_fd = open(o.output, flags, 0666);
		if (out_fd < 0) {
			brk_cmd_error("%s: %s", o.output, strerror(errno));
			goto out;
		}
	}

	if (o.skip) {
		off_t off = (off_t)(o.skip * o.block_size);

		if (lseek(in_fd, off, SEEK_SET) < 0) {
			unsigned char discard[4096];
			unsigned long long left = o.skip * o.block_size;

			while (left) {
				size_t n = left < sizeof(discard) ?
						   (size_t)left :
						   sizeof(discard);
				ssize_t r = read(in_fd, discard, n);

				if (r < 0) {
					if (errno == EINTR)
						continue;
					brk_cmd_error("%s: %s", o.input,
						      strerror(errno));
					goto out;
				}
				if (r == 0) {
					brk_cmd_error("%s: unexpected EOF",
						      o.input);
					goto out;
				}
				left -= (unsigned long long)r;
			}
		}
	}

	if (o.seek) {
		if (lseek(out_fd, (off_t)(o.seek * o.block_size), SEEK_SET) <
		    0) {
			brk_cmd_error("%s: %s", o.output ? o.output : "stdout",
				      strerror(errno));
			goto out;
		}
	}

	buf = malloc(o.block_size);
	if (!buf) {
		brk_cmd_error("out of memory");
		goto out;
	}

	for (;;) {
		size_t used = 0;

		if (o.count_set && in_records >= o.count)
			break;

		while (used < o.block_size) {
			ssize_t n =
				read(in_fd, buf + used, o.block_size - used);

			if (n < 0) {
				if (errno == EINTR)
					continue;
				brk_cmd_error("%s: %s",
					      o.input ? o.input : "stdin",
					      strerror(errno));
				goto out;
			}
			if (n == 0)
				break;
			used += (size_t)n;
		}

		if (used == 0)
			break;

		size_t wr = used;

		if (o.sync && used < o.block_size) {
			memset(buf + used, 0, o.block_size - used);
			wr = o.block_size;
		}

		if (write_all(out_fd, buf, wr) != 0) {
			brk_cmd_error("%s: %s", o.output ? o.output : "stdout",
				      strerror(errno));
			goto out;
		}

		in_records++;
		out_records++;
		if (used < o.block_size)
			break;
	}

	ret = BRK_CMD_EXIT_OK;

out:
	if (buf)
		free(buf);
	if (in_fd != STDIN_FILENO)
		close(in_fd);
	if (out_fd != STDOUT_FILENO)
		close(out_fd);
	if (o.status) {
		fprintf(stderr, "%llu+0 records in\n", in_records);
		fprintf(stderr, "%llu+0 records out\n", out_records);
	}
	return ret;
}
