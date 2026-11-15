#include "pthread_impl.h"

static struct pthread main_thread;

struct pthread *__pthread_self_internal(void)
{
	uintptr_t tp = __pthread_get_tp();

	return tp ? (struct pthread *)tp : &main_thread;
}

pthread_t pthread_self(void)
{
	return __pthread_self_internal();
}

int *__errno_location(void)
{
	return &__pthread_self_internal()->err;
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
	return t1 == t2;
}
