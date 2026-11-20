#include "pthread_impl.h"

#include <errno.h>

#define NS_PER_SEC 1000000000L

static int cond_remaining(const struct timespec *abstime, clockid_t clock,
			  struct timespec *remaining)
{
	struct timespec now;

	if (clock_gettime(clock, &now) != 0)
		return -1;

	remaining->tv_sec = abstime->tv_sec - now.tv_sec;
	remaining->tv_nsec = abstime->tv_nsec - now.tv_nsec;
	if (remaining->tv_nsec < 0) {
		remaining->tv_sec--;
		remaining->tv_nsec += NS_PER_SEC;
	}
	if (remaining->tv_sec < 0 ||
	    (remaining->tv_sec == 0 && remaining->tv_nsec <= 0))
		return 0;
	return 1;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
	if (!cond)
		return EINVAL;

	cond->__seq = 0;
	cond->__clock = attr ? attr->clock : CLOCK_REALTIME;
	return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
	if (!cond)
		return EINVAL;

	return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	int seq;

	if (!cond || !mutex)
		return EINVAL;

	seq = __atomic_load_n(&cond->__seq, __ATOMIC_ACQUIRE);
	if (pthread_mutex_unlock(mutex) != 0)
		return EINVAL;

	for (;;) {
		long err = __pthread_futex_wait(&cond->__seq, seq, NULL);

		if (err == 0)
			break;
		if (errno == EAGAIN)
			break;
		if (errno == EINTR)
			continue;
		break;
	}

	pthread_mutex_lock(mutex);
	return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
			   const struct timespec *abstime)
{
	struct timespec remaining;
	int seq;
	int ret = 0;

	if (!cond || !mutex || !abstime)
		return EINVAL;

	seq = __atomic_load_n(&cond->__seq, __ATOMIC_ACQUIRE);
	if (pthread_mutex_unlock(mutex) != 0)
		return EINVAL;

	for (;;) {
		int state = cond_remaining(abstime, cond->__clock, &remaining);
		long err;

		if (state < 0) {
			ret = errno;
			break;
		}
		if (state == 0) {
			ret = ETIMEDOUT;
			break;
		}

		err = __pthread_futex_wait(&cond->__seq, seq, &remaining);
		if (err == 0)
			break;
		if (errno == EAGAIN)
			break;
		if (errno == ETIMEDOUT) {
			ret = ETIMEDOUT;
			break;
		}
		if (errno == EINTR)
			continue;
		ret = errno;
		break;
	}

	pthread_mutex_lock(mutex);
	return ret;
}

int pthread_cond_signal(pthread_cond_t *cond)
{
	if (!cond)
		return EINVAL;

	__atomic_add_fetch(&cond->__seq, 1, __ATOMIC_RELEASE);
	(void)__pthread_futex_wake(&cond->__seq, 1);
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
	if (!cond)
		return EINVAL;

	__atomic_add_fetch(&cond->__seq, 1, __ATOMIC_RELEASE);
	(void)__pthread_futex_wake(&cond->__seq, 0x7fffffff);
	return 0;
}

int pthread_condattr_init(pthread_condattr_t *attr)
{
	if (!attr)
		return EINVAL;

	attr->clock = CLOCK_REALTIME;
	return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
	if (!attr)
		return EINVAL;

	attr->clock = CLOCK_REALTIME;
	return 0;
}

int pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock)
{
	if (!attr)
		return EINVAL;
	if (clock != CLOCK_REALTIME && clock != CLOCK_MONOTONIC)
		return EINVAL;

	attr->clock = clock;
	return 0;
}

int pthread_condattr_getclock(const pthread_condattr_t *attr, clockid_t *clock)
{
	if (!attr || !clock)
		return EINVAL;

	*clock = attr->clock;
	return 0;
}
