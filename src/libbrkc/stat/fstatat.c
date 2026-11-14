#include "statx_impl.h"

int fstatat(int dirfd, const char *path, struct stat *buf, int flags)
{
	return statx_at(dirfd, path, buf, flags);
}
