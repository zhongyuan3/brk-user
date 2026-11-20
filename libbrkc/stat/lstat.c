#include "statx_impl.h"

int lstat(const char *path, struct stat *buf)
{
	return statx_at(AT_FDCWD, path, buf, AT_SYMLINK_NOFOLLOW);
}
