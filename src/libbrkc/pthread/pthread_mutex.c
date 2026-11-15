#include "pthread_impl.h"

#include <errno.h>

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	(void)attr;
	if (!mutex)
		return EINVAL;

	mutex->__lock = 0;
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	if (!mutex)
		return EINVAL;

	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	int expected = 0;
	int old;

	if (!mutex)
		return EINVAL;

	if (__atomic_compare_exchange_n(&mutex->__lock, &expected, 1, 0,
					__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return 0;

	for (;;) {
		/*
		 * Exchange, rather than store, so an unlock between the failed
		 * CAS and this point is observed as old == 0 and acquired
		 * instead of leaving an unowned lock marked contended.
		 */
		old = __atomic_exchange_n(&mutex->__lock, 2, __ATOMIC_ACQUIRE);
		if (old == 0)
			return 0;

		(void)__pthread_futex_wait(&mutex->__lock, 2, NULL);

		expected = 0;
		if (__atomic_compare_exchange_n(&mutex->__lock, &expected, 1, 0,
						__ATOMIC_ACQUIRE,
						__ATOMIC_RELAXED))
			return 0;
	}
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	int expected = 0;

	if (!mutex)
		return EINVAL;

	if (__atomic_compare_exchange_n(&mutex->__lock, &expected, 1, 0,
					__ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		return 0;
	return EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	int old;

	if (!mutex)
		return EINVAL;

	old = __atomic_exchange_n(&mutex->__lock, 0, __ATOMIC_RELEASE);
	if (old == 2)
		(void)__pthread_futex_wake(&mutex->__lock, 1);
	return 0;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
	if (!attr)
		return EINVAL;

	attr->type = PTHREAD_MUTEX_NORMAL;
	return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
	if (!attr)
		return EINVAL;

	attr->type = PTHREAD_MUTEX_NORMAL;
	return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
	if (!attr)
		return EINVAL;
	if (type != PTHREAD_MUTEX_NORMAL)
		return EINVAL;

	attr->type = type;
	return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)
{
	if (!attr || !type)
		return EINVAL;

	*type = attr->type;
	return 0;
}
