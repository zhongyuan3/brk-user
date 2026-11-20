#include <brk/cmd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char usage[] = "Usage: uuencode [-m] [FILE] NAME\n"
			    "Encode FILE (or standard input) for uudecode.";

static const char usage2[] = "Usage: uuencode [FILE] NAME";

static void encode_line(const unsigned char *buf, size_t n)
{
	char out[64];
	size_t oi = 0;

	out[oi++] = (char)(' ' + n);
	for (size_t i = 0; i < n; i += 3) {
		unsigned int v = (unsigned int)buf[i] << 16;

		if (i + 1 < n)
			v |= (unsigned int)buf[i + 1] << 8;
		if (i + 2 < n)
			v |= buf[i + 2];
		out[oi++] = (char)(' ' + ((v >> 18) & 0x3f));
		out[oi++] = (char)(' ' + ((v >> 12) & 0x3f));
		out[oi++] = (char)(' ' + ((v >> 6) & 0x3f));
		out[oi++] = (char)(' ' + (v & 0x3f));
	}
	out[oi++] = '\n';
	fwrite(out, 1, oi, stdout);
}

int main(int argc, char *argv[])
{
	const char *path = NULL;
	const char *name;

	brk_cmd_init(argc, argv);

	if (argc > 1 && !strcmp(argv[1], "--help"))
		brk_cmd_help_exit(usage);
	if (argc > 1 && !strcmp(argv[1], "-m"))
		brk_cmd_usage(usage2);
	if (argc == 2) {
		name = argv[1];
	} else if (argc == 3) {
		path = argv[1];
		name = argv[2];
	} else {
		brk_cmd_usage(usage);
	}

	FILE *fp = path ? fopen(path, "r") : stdin;

	if (!fp)
		return brk_cmd_fail_errno(path);

	printf("begin 644 %s\n", name);

	unsigned char buf[45];
	for (;;) {
		size_t n = fread(buf, 1, sizeof(buf), fp);

		if (!n)
			break;
		encode_line(buf, n);
		if (n < sizeof(buf))
			break;
	}
	printf("`\nend\n");

	if (fp != stdin)
		fclose(fp);
	return BRK_CMD_EXIT_OK;
}
