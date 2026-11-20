#include <stdio.h>
#include <sys/statvfs.h>

int main(int argc, char *argv[])
{
	const char *path = argc > 1 ? argv[1] : "/";
	struct statvfs st;

	if (statvfs(path, &st) < 0) {
		perror("statvfs");
		return 1;
	}

	printf("Filesystem %s\n", path);
	printf("  block size: %lu\n", st.f_bsize);
	printf("  fragment size: %lu\n", st.f_frsize);
	printf("  blocks:     %lu total, %lu free, %lu available\n",
	       st.f_blocks, st.f_bfree, st.f_bavail);
	printf("  inodes:     %lu total, %lu free\n", st.f_files, st.f_ffree);
	printf("  namemax:    %lu\n", st.f_namemax);
	return 0;
}
