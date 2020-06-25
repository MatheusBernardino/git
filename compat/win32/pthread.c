/*
 * Copyright (C) 2009 Andrzej K. Haczewski <ahaczewski@gmail.com>
 *
 * DISCLAIMER: The implementation is Git-specific, it is subset of original
 * Pthreads API, without lots of other features that Git doesn't use.
 * Git also makes sure that the passed arguments are valid, so there's
 * no need for double-checking.
 */

#include "../../git-compat-util.h"
#include "pthread.h"

#include <errno.h>
#include <limits.h>

static unsigned __stdcall win32_start_routine(void *arg)
{
	pthread_t *thread = arg;
	thread->tid = GetCurrentThreadId();
	thread->arg = thread->start_routine(thread->arg);
	return 0;
}

int pthread_create(pthread_t *thread, const void *unused,
		   void *(*start_routine)(void*), void *arg)
{
	thread->arg = arg;
	thread->start_routine = start_routine;
	thread->handle = (HANDLE)
		_beginthreadex(NULL, 0, win32_start_routine, thread, 0, NULL);

	if (!thread->handle)
		return errno;
	else
		return 0;
}

int win32_pthread_join(pthread_t *thread, void **value_ptr)
{
	DWORD result = WaitForSingleObject(thread->handle, INFINITE);
	switch (result) {
		case WAIT_OBJECT_0:
			if (value_ptr)
				*value_ptr = thread->arg;
			return 0;
		case WAIT_ABANDONED:
			return EINVAL;
		default:
			return err_win_to_posix(GetLastError());
	}
}

pthread_t pthread_self(void)
{
	pthread_t t = { NULL };
	t.tid = GetCurrentThreadId();
	return t;
}

struct fls_entry {
	void *value;
	pthread_key_t *key;
};

static void fls_entry_destructor(void *arg)
{
	struct fls_entry *entry = arg;
	void (*destructor)(void *) = entry->key->destructor;

	if (entry->value && destructor)
		destructor(entry->value);

	FREE_AND_NULL(entry);
}

int pthread_key_create(pthread_key_t *keyp, void (*destructor)(void *value))
{
	keyp->destructor = destructor;
	keyp->index = FlsAlloc(fls_entry_destructor);
	return keyp->index == FLS_OUT_OF_INDEXES ? EAGAIN : 0;
}

int pthread_key_delete(pthread_key_t key)
{
	/*
	 * POSIX specifies that: "No destructor functions shall be invoked by
	 * pthread_key_delete(). Any destructor function that may have been
	 * associated with key shall no longer be called upon thread exit."
	 */
	key.destructor = NULL;
	return FlsFree(key.index) ? 0 : EINVAL;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
	struct fls_entry *entry = FlsGetValue(key.index);

	if (!entry) {
		entry = xmalloc(sizeof(*entry));
		entry->key = &key;
	}

	entry->value = (void *)value;

	if (!FlsSetValue(key.index, (void *)entry)) {
		free(entry);
		return EINVAL;
	}

	return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
	struct fls_entry *entry = FlsGetValue(key.index);
	if (entry)
		return entry->value;
	return NULL;
}

/* Adapted from libav's compat/w32pthreads.h. */
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
	BOOL pending = FALSE;
	int ret = 0;

	if (!InitOnceBeginInitialize(once_control, 0, &pending, NULL)) {
		ret = err_win_to_posix(GetLastError());
	} else if (pending) {
		init_routine();
		if (!InitOnceComplete(once_control, 0, NULL))
			ret = err_win_to_posix(GetLastError());
	}

	/* POSIX doesn't allow pthread_once() to return EINTR */
	return ret == EINTR ? EIO : ret;
}
