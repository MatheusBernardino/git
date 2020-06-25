#ifndef THREAD_COMPAT_H
#define THREAD_COMPAT_H

#ifndef NO_PTHREADS
#include <pthread.h>

#define HAVE_THREADS 1

#else

#define HAVE_THREADS 0

/*
 * macros instead of typedefs because pthread definitions may have
 * been pulled in by some system dependencies even though the user
 * wants to disable pthread.
 */
#define pthread_t int
#define pthread_mutex_t int
#define pthread_cond_t int
#define pthread_key_t void *
#define pthread_once_t int

#define pthread_mutex_init(mutex, attr) dummy_pthread_init(mutex)
#define pthread_mutex_lock(mutex)
#define pthread_mutex_unlock(mutex)
#define pthread_mutex_destroy(mutex)

#define pthread_cond_init(cond, attr) dummy_pthread_init(cond)
#define pthread_cond_wait(cond, mutex)
#define pthread_cond_signal(cond)
#define pthread_cond_broadcast(cond)
#define pthread_cond_destroy(cond)

/*
 * The destructor is not used in this case as the main thread will only
 * exit when the program terminates.
 */
#define pthread_key_create(key_ptr, unused) return_0((*key_ptr) = NULL)
#define pthread_setspecific(key, value) return_0((key) = (value))
#define pthread_getspecific(key) (key)
#define pthread_key_delete(key) return_0(NULL)

static inline int return_0(void *unused)
{
	return 0;
}

#define pthread_create(thread, attr, fn, data) \
	dummy_pthread_create(thread, attr, fn, data)
#define pthread_join(thread, retval) \
	dummy_pthread_join(thread, retval)

int dummy_pthread_create(pthread_t *pthread, const void *attr,
			 void *(*fn)(void *), void *data);
int dummy_pthread_join(pthread_t pthread, void **retval);

int dummy_pthread_init(void *);

#define PTHREAD_ONCE_INIT 0
#define pthread_once(once, routine) nothreads_pthread_once(once, routine)

int nothreads_pthread_once(pthread_once_t *once_control,
			   void (*init_routine)(void));

#endif

int online_cpus(void);
int init_recursive_mutex(pthread_mutex_t*);


#endif /* THREAD_COMPAT_H */
