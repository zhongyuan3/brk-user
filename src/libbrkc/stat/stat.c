#include "statx_impl.h"

int stat(const char *path, struct stat *buf)
{
	return statx_at(AT_FDCWD, path, buf, 0);
}
