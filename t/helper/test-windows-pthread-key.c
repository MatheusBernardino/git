#include "test-tool.h"
#include "git-compat-util.h"
#include "thread-utils.h"

/*
 * This program tests the pthread_key emulation for Windows.
 *
 * Usage: test-tool windows-pthread-key [opts] tls_test_value
 *
 * The program spawns one additional thread and passes the given tls_test_value
 * argument to it. The thread then attempts to store this value in its
 * thread-local storage and retrieve the result, which is printed to stdout.
 * When --no-set or --set-to-null are used, tls_test_value should not be given.
 * Also, when printing NULL, the program will use the string "(null)".
 *
 * Valid options are:
 *
 * --destructor: call pthread_key_create() with a callback function that prints
 *		 "destructor received \"%s\"", where %s is the value passed to
 *		 the callback.
 *
 * --delete-key-in-thread: instead of deleting the TLS key after the thread
 *			   exits, instruct the thread to delete it. According
 *			   to POSIX, no destructor callback should be called in
 *			   this case. So this option is useful in conjunction
 *			   with --destructor to test if this behavior is
 *			   correctly implemented.
 *
 * --no-set: don't set any value in TLS. Useful to test the return of
 *	     pthread_getspecific() when no previous value is set.
 *
 * --set-to-null: explicitly set NULL to TLS.
 *
 * Note: --no-set --set-to-null are not compatible together. In practice, they
 * should have the same effect, but --set-to-null is provided for cases where
 * it is desired to test the behavior when explicitly setting the value to NULL.
 */

#ifdef GIT_WINDOWS_NATIVE

static pthread_key_t key;
static pthread_t thread;
static int delete_key_in_thread, use_destructor, set_to_null, no_set;

#define NULL_STR "(null)"

#define CHECK_ERR(err, msg) \
	do { \
		int e = (err); \
		if (e) \
			die("%s: %s", msg, strerror(e)); \
	} while(0)

static void destructor_callback(void *arg)
{
	char *value = arg;
	printf("destructor received \"%s\"\n", value ? value : NULL_STR);
}

static void *thread_routine(void *tls_test_value)
{
	char *retrieved_value;

	if (!no_set) {
		void *value_to_store = set_to_null ? NULL : tls_test_value; 
		CHECK_ERR(pthread_setspecific(key, value_to_store),
			  "pthread_setspecific error");
	}

	retrieved_value = pthread_getspecific(key);
	printf("%s\n", retrieved_value ? retrieved_value : NULL_STR);

	if (delete_key_in_thread)
		CHECK_ERR(pthread_key_delete(key), "pthread_key_delete error");

	return NULL;
}

static void setup_and_run(char *tls_test_value)
{
	void (*callback)(void *) = NULL;

	if (use_destructor)
		callback = destructor_callback;

	CHECK_ERR(pthread_key_create(&key, callback), "pthread_key_create error");
	CHECK_ERR(pthread_create(&thread, NULL, thread_routine, tls_test_value),
		  "pthread_create error");
	CHECK_ERR(pthread_join(thread, NULL), "pthread_join error");

	if (!delete_key_in_thread)
		CHECK_ERR(pthread_key_delete(key), "pthread_key_delete error");
}

int cmd__windows_pthread_key(int argc, const char **argv)
{
	char *tls_test_value = NULL;

	for (++argv, --argc; *argv && starts_with(*argv, "--"); ++argv, --argc) {
		if (!strcmp(*argv, "--delete-key-in-thread"))
			delete_key_in_thread = 1;
		else if (!strcmp(*argv, "--destructor"))
			use_destructor = 1;
		else if (!strcmp(*argv, "--no-set"))
			no_set = 1;
		else if (!strcmp(*argv, "--set-to-null"))
			set_to_null = 1;
		else
			die("invalid option '%s'", *argv);
	}

	if (set_to_null && no_set)
		die("--no-set and --set-to-null are incompatible.");

	if (set_to_null || no_set) {
		if (argc)
			die("value parameter is not accepted with --no-set and --set-to-null");
	} else if (argc == 1) {
		tls_test_value = (char *)*argv;
	} else {
		die("one (and only one) parameter is required after options: tls_test_value");
	}

	setup_and_run(tls_test_value);

	return 0;
}

#endif /* GIT_WINDOWS_NATIVE */
