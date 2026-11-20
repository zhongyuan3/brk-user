#include <brk/cmd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: cksum [FILE...]\n"
			    "Print POSIX CRC checksum and size for each FILE.";

static uint32_t crc_byte(uint32_t crc, unsigned char c)
{
	crc ^= (uint32_t)c << 24;
	for (int i = 0; i < 8; i++) {
		if (crc & 0x80000000u)
			crc = (crc << 1) ^ 0x04c11db7u;
		else
			crc <<= 1;
	}
	return crc;
}

static int cksum_fd(int fd, const char *name)
{
	unsigned char buf[4096];
	uint32_t crc = 0;
	uint64_t size = 0;

	for (;;) {
		ssize_t n = read(fd, buf, sizeof(buf));

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (n == 0)
			break;
		for (ssize_t i = 0; i < n; i++)
			crc = crc_byte(crc, buf[i]);
		size += (uint64_t)n;
	}

	uint64_t len = size;

	while (len) {
		crc = crc_byte(crc, (unsigned char)(len & 0xff));
		len >>= 8;
	}
	crc = ~crc;

	printf("%lu %llu", (unsigned long)crc, (unsigned long long)size);
	if (name)
		printf(" %s", name);
	putchar('\n');
	return 0;
}

int main(int argc, char *argv[])
{
	int ret = BRK_CMD_EXIT_OK;

	brk_cmd_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(usage);

	if (argc == 1) {
		if (cksum_fd(STDIN_FILENO, NULL) != 0)
			return brk_cmd_fail_errno("stdin");
		return BRK_CMD_EXIT_OK;
	}

	for (int i = 1; i < argc; i++) {
		const char *path = argv[i];
		int fd;

		if (!strcmp(path, "-")) {
			fd = STDIN_FILENO;
		} else {
			fd = open(path, O_RDONLY);
			if (fd < 0) {
				brk_cmd_error("%s: %s", path, strerror(errno));
				ret = BRK_CMD_EXIT_FAIL;
				continue;
			}
		}

		if (cksum_fd(fd, path) != 0) {
			brk_cmd_error("%s: %s", path, strerror(errno));
			ret = BRK_CMD_EXIT_FAIL;
		}
		if (fd != STDIN_FILENO)
			close(fd);
	}

	return ret;
}
