#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	int fd;
	long offset;
	long len;
	int err;

	if (argc < 4) {
		fprintf(stderr, "usage: fallocate <file> <offset> <length>\n");
		return 1;
	}

	offset = strtol(argv[2], NULL, 0);
	len = strtol(argv[3], NULL, 0);

	fd = open(argv[1], O_WRONLY | O_CREAT, 0644);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	err = posix_fallocate(fd, offset, len);
	if (err) {
		fprintf(stderr, "fallocate: %s\n", strerror(err));
		close(fd);
		return 1;
	}

	close(fd);
	return 0;
}
