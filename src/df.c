#include <stdio.h>
#include <sys/statfs.h>

int main(int argc, char *argv[])
{
	const char *path = argc > 1 ? argv[1] : "/";
	struct statfs st;

	if (statfs(path, &st) < 0) {
		perror("statfs");
		return 1;
	}

	printf("Filesystem %s\n", path);
	printf("  type:       %ld\n", st.f_type);
	printf("  block size: %ld\n", st.f_bsize);
	printf("  blocks:     %ld total, %ld free, %ld available\n",
	       st.f_blocks, st.f_bfree, st.f_bavail);
	printf("  inodes:     %ld total, %ld free\n", st.f_files,
	       st.f_ffree);
	printf("  namelen:    %ld\n", st.f_namelen);
	return 0;
}
