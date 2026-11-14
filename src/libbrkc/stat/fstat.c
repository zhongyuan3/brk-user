#include "statx_impl.h"

int fstat(int fd, struct stat *buf)
{
	return statx_at(fd, "", buf, AT_EMPTY_PATH);
}
