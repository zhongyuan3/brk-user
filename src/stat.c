#include <apputil.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char usage[] = "Usage: stat FILE...";

static void mode_string(unsigned mode, char out[11])
{
	memset(out, '-', 10);
	out[10] = '\0';

	if (S_ISDIR(mode))
		out[0] = 'd';
	else if (S_ISLNK(mode))
		out[0] = 'l';
	else if (S_ISCHR(mode))
		out[0] = 'c';
	else if (S_ISBLK(mode))
		out[0] = 'b';
	else if (S_ISFIFO(mode))
		out[0] = 'p';
	else if (S_ISSOCK(mode))
		out[0] = 's';

	if (mode & S_IRUSR)
		out[1] = 'r';
	if (mode & S_IWUSR)
		out[2] = 'w';
	if (mode & S_IXUSR)
		out[3] = 'x';
	if (mode & S_IRGRP)
		out[4] = 'r';
	if (mode & S_IWGRP)
		out[5] = 'w';
	if (mode & S_IXGRP)
		out[6] = 'x';
	if (mode & S_IROTH)
		out[7] = 'r';
	if (mode & S_IWOTH)
		out[8] = 'w';
	if (mode & S_IXOTH)
		out[9] = 'x';
}

int main(int argc, char *argv[])
{
	struct app_optctx ctx;
	int ret = APP_EXIT_OK;

	app_init(argc, argv);
	app_optctx_init(&ctx, argc, argv);

	if (app_operand_count(&ctx) == 0)
		app_usage(usage);

	for (int i = 0; i < app_operand_count(&ctx); i++) {
		const char *path = app_operand(&ctx, i);
		struct stat st;
		char mode[11];

		if (stat(path, &st) != 0) {
			ret = app_fail_errno("cannot stat");
			continue;
		}

		mode_string(st.st_mode, mode);
		printf("  File: %s\n", path);
		printf("  Size: %-10ld Blocks: %-8ld Inode: %lu Links: %u\n",
		       st.st_size, st.st_blocks, st.st_ino, st.st_nlink);
		printf("  Mode: %s Uid: %u Gid: %u\n", mode, st.st_uid,
		       st.st_gid);
		printf("Modify: %ld\n", st.st_mtime);
		printf("Change: %ld\n", st.st_ctime);
		printf("Access: %ld\n", st.st_atime);
	}

	return ret;
}
