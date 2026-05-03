#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
	if (argc != 2) {
		fprintf(stderr, "Usage: unlink <linkname>\n");
		return 1;
	}

	int err = unlink(argv[1]);
	if (err) {
		fprintf(stderr, "unlink: %s failed: %s\n", argv[1],
			strerror(err));
		return 1;
	}

	return 0;
}
