#include "pthread_impl.h"

#include <errno.h>

int pthread_attr_init(pthread_attr_t *attr)
{
	if (!attr)
		return EINVAL;

	attr->stacksize = PTHREAD_STACK_DEFAULT;
	return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
	if (!attr)
		return EINVAL;

	attr->stacksize = 0;
	return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
	if (!attr || stacksize < PTHREAD_STACK_MIN)
		return EINVAL;

	attr->stacksize = stacksize;
	return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)
{
	if (!attr || !stacksize)
		return EINVAL;

	*stacksize = attr->stacksize ? attr->stacksize : PTHREAD_STACK_DEFAULT;
	return 0;
}
