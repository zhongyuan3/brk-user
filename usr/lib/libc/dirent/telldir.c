#include "dirent_impl.h"

long telldir(DIR *d)
{
	if (!d)
		return -1;
	return d->tell;
}
