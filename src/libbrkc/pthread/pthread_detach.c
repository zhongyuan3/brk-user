#include "pthread_impl.h"

#include <errno.h>
#include <stdbool.h>
#include <sys/mman.h>

static pthread_mutex_t detach_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t reaper_lock = PTHREAD_MUTEX_INITIALIZER;
static struct pthread *detach_head;
static int detach_seq;
static int reaper_started;

static void detach_enqueue_locked(struct pthread *thread)
{
	thread->detach_next = detach_head;
	detach_head = thread;
	thread->detach_queued = 1;
	__atomic_add_fetch(&detach_seq, 1, __ATOMIC_RELEASE);
}

static void *reaper_main(void *arg)
{
	(void)arg;

	for (;;) {
		struct pthread *list;
		struct pthread *thread;
		struct pthread *next;
		int seen;

		pthread_mutex_lock(&detach_lock);
		list = detach_head;
		detach_head = NULL;
		seen = __atomic_load_n(&detach_seq, __ATOMIC_ACQUIRE);
		pthread_mutex_unlock(&detach_lock);

		if (!list) {
			(void)__pthread_futex_wait(&detach_seq, seen, NULL);
			continue;
		}

		for (thread = list; thread; thread = next) {
			int tid;

			next = thread->detach_next;
			while ((tid = __atomic_load_n(&thread->tid,
						      __ATOMIC_ACQUIRE)) != 0)
				(void)__pthread_futex_wait(&thread->tid, tid,
							   NULL);
			if (thread->stack)
				munmap(thread->stack, thread->stack_size);
		}
	}

	return NULL;
}

int __pthread_reaper_start(void)
{
	pthread_attr_t attr;
	pthread_t thread;
	int err;

	pthread_mutex_lock(&reaper_lock);
	if (reaper_started) {
		pthread_mutex_unlock(&reaper_lock);
		return 0;
	}

	(void)pthread_attr_init(&attr);
	(void)pthread_attr_setstacksize(&attr, 64 * 1024);
	err = pthread_create(&thread, &attr, reaper_main, NULL);
	(void)pthread_attr_destroy(&attr);
	if (!err)
		reaper_started = 1;
	pthread_mutex_unlock(&reaper_lock);
	return err;
}

void __pthread_detach_cleanup(struct pthread *self)
{
	int wake = 0;

	pthread_mutex_lock(&detach_lock);
	self->exiting = 1;
	if (self->detached && self->stack && !self->detach_queued) {
		detach_enqueue_locked(self);
		wake = 1;
	}
	pthread_mutex_unlock(&detach_lock);

	if (wake)
		(void)__pthread_futex_wake(&detach_seq, 1);
}

int pthread_detach(pthread_t thread)
{
	bool wake = false;
	int err;

	if (!thread)
		return EINVAL;

	err = __pthread_reaper_start();
	if (err)
		return err;

	pthread_mutex_lock(&detach_lock);
	thread->detached = 1;
	if (thread->exiting && thread->stack && !thread->detach_queued) {
		detach_enqueue_locked(thread);
		wake = true;
	}
	pthread_mutex_unlock(&detach_lock);

	if (wake)
		(void)__pthread_futex_wake(&detach_seq, 1);
	return 0;
}
