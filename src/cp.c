#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct cp_args {
	const char *src;
	const char *dest;
};

static void help(int exit_status)
{
	fprintf(stderr, "Usage: cp <src> <dest>\n");
	_exit(exit_status);
}

static void usage(void)
{
	help(2);
}

static int parse_args(int argc, char *argv[], struct cp_args *args)
{
	memset(args, 0, sizeof(*args));

	if (argc < 2)
		usage();

	for (int i = 1; i < argc; i++) {
		if (argv[i][0] != '-') {
			if (!args->src)
				args->src = argv[i];
			else if (!args->dest)
				args->dest = argv[i];
			else
				return 1;
			continue;
		}
		if (!strcmp(argv[i], "--help")) {
			help(0);
			return 0;
		}
		return 1;
	}
	return 0;
}

static int cp(const char *src, const char *dest)
{
	char buf[1024];
	int ret = 0;
	int fd_src = open(src, O_RDONLY);
	if (fd_src < 0) {
		perror("cp: open failed");
		return 1;
	}
	int fd_dest = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd_dest < 0) {
		perror("cp: open failed");
		return 1;
	}

	while (1) {
		ssize_t rcnt = read(fd_src, buf, sizeof(buf));
		if (rcnt < 0) {
			perror("cp: read failed");
			ret = 1;
			break;
		}
		if (rcnt < 1)
			break;
		ssize_t wcnt = write(fd_dest, buf, rcnt);
		if (wcnt < 0) {
			perror("cp: write failed");
			ret = 1;
			break;
		}
	}

	close(fd_src);
	close(fd_dest);
	return ret;
}

int main(int argc, char *argv[])
{
	struct cp_args args;
	int err = parse_args(argc, argv, &args);
	if (err)
		return err;

	return cp(args.src, args.dest);
}
