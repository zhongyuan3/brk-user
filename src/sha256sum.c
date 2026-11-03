#include <apputil.h>
#include <errno.h>
#include <fcntl.h>
#include <pwutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char help[] =
	"Usage: sha256sum [FILE...]\n"
	"Print the SHA-256 checksum of each FILE, or standard input.";

static int read_all(int fd, unsigned char **out, size_t *out_len)
{
	size_t cap = 4096;
	size_t len = 0;
	unsigned char *buf = malloc(cap);

	if (!buf)
		return -1;

	for (;;) {
		if (len == cap) {
			if (cap > (size_t)-1 / 2) {
				free(buf);
				errno = ENOMEM;
				return -1;
			}
			cap *= 2;

			unsigned char *nb = realloc(buf, cap);

			if (!nb) {
				free(buf);
				return -1;
			}
			buf = nb;
		}

		ssize_t n = read(fd, buf + len, cap - len);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			free(buf);
			return -1;
		}
		if (n == 0)
			break;
		len += (size_t)n;
	}

	*out = buf;
	*out_len = len;
	return 0;
}

static int hash_file(const char *name)
{
	unsigned char *data;
	size_t len;
	unsigned char digest[32];
	int fd;

	if (!strcmp(name, "-")) {
		fd = STDIN_FILENO;
	} else {
		fd = open(name, O_RDONLY);
		if (fd < 0)
			return app_fail_errno(name);
	}

	if (read_all(fd, &data, &len) != 0) {
		if (fd != STDIN_FILENO)
			close(fd);
		return app_fail_errno(name);
	}
	if (fd != STDIN_FILENO)
		close(fd);

	sha256(data, len, digest);
	free(data);

	for (size_t i = 0; i < sizeof(digest); i++)
		printf("%02x", digest[i]);
	printf("  %s\n", name);
	return APP_EXIT_OK;
}

int main(int argc, char *argv[])
{
	int ret = APP_EXIT_OK;

	app_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		app_help_exit(help);

	if (argc == 1)
		return hash_file("-");

	for (int i = 1; i < argc; i++) {
		if (hash_file(argv[i]) != APP_EXIT_OK)
			ret = APP_EXIT_FAIL;
	}
	return ret;
}
