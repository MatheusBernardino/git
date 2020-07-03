#include "test-tool.h"
#include "git-compat-util.h"
#include "thread-utils.h"

#ifdef GIT_WINDOWS_NATIVE

#define WPK_INITIAL_TLS_VALUE 2

enum wpk_cmd { WPK_SETUP_ONLY, WPK_FIRST_GET, WPK_SET_AND_GET, WPK_DESTRUCTOR,
	       WPK_DESTRUCTOR_NULL, WPK_DELETE };

static enum wpk_cmd cmd;
static pthread_key_t key;
static pthread_t thread;
static int tls_value = WPK_INITIAL_TLS_VALUE;

const char *wpk_usage = "test-tool windows-pthread-key <cmd>";

#define CHECK_ERR(err, msg) \
	do { \
		int e = (err); \
		if (e) \
			die("%s: %s", msg, strerror(e)); \
	} while(0)

#define WPK_ASSERT(cond) \
	do { \
		if(!(cond)) \
			die("Assertion failed: `" #cond "`"); \
	} while(0)

static void inc_callback(void *arg)
{
	int *value = arg;
	if (!value)
		BUG("pthread_create_key's callback called for NULL value");
	(*value)++;
}

static void *thread_routine(void *arg)
{
	int value;

	switch (cmd) {
	case WPK_SETUP_ONLY:
		break;
	case WPK_FIRST_GET:
		WPK_ASSERT(pthread_getspecific(key) == NULL);
		break;
	case WPK_SET_AND_GET:
		value = WPK_INITIAL_TLS_VALUE;

		CHECK_ERR(pthread_setspecific(key, (void *)(intptr_t)value),
			  "pthread_setspecific error");
		WPK_ASSERT((int)(intptr_t)pthread_getspecific(key) == value);
		break;
	case WPK_DESTRUCTOR:
		CHECK_ERR(pthread_setspecific(key, (void *)&tls_value),
			  "pthread_setspecific error");
		break;
	case WPK_DESTRUCTOR_NULL:
		/* callback should not be called */
		CHECK_ERR(pthread_setspecific(key, NULL),
			  "pthread_setspecific error");
		break;
	case WPK_DELETE:
		CHECK_ERR(pthread_setspecific(key, (void *)&tls_value),
			  "pthread_setspecific error");
		CHECK_ERR(pthread_key_delete(key),
			  "pthread_key_delete error");
		break;
	default:
		usage(wpk_usage);
	}

	return NULL;
}

static void setup_and_run(void)
{
	void (*callback)(void *) = NULL;

	if (cmd == WPK_DESTRUCTOR || cmd == WPK_DESTRUCTOR_NULL ||
	    cmd == WPK_DELETE) {
		callback = inc_callback;
	}

	CHECK_ERR(pthread_key_create(&key, callback),
		  "pthread_key_create error");
	CHECK_ERR(pthread_create(&thread, NULL, thread_routine, NULL),
		  "pthread_create error");
	CHECK_ERR(pthread_join(thread, NULL),
		  "pthread_join error");

	if (cmd == WPK_DESTRUCTOR) {
		WPK_ASSERT(tls_value == WPK_INITIAL_TLS_VALUE + 1);
	} else if (cmd == WPK_DELETE) {
		/* callback should not be called if the key was deleted */
		WPK_ASSERT(tls_value == WPK_INITIAL_TLS_VALUE);
	}

	if (cmd != WPK_DELETE) {
		CHECK_ERR(pthread_key_delete(key),
			  "pthread_key_delete error");
	}
}

int cmd__windows_pthread_key(int argc, const char **argv)
{
	if (argc != 2)
		goto wrong_usage;

	if (!strcmp(argv[1], "setup-only")
		cmd = WPK_SETUP_ONLY;
	else if (!strcmp(argv[1], "first-get"))
		cmd = WPK_FIRST_GET;
	else if (!strcmp(argv[1], "set-and-get"))
		cmd = WPK_SET_AND_GET;
	else if (!strcmp(argv[1], "destructor"))
		cmd = WPK_DESTRUCTOR;
	else if (!strcmp(argv[1], "destructor-on-null"))
		cmd = WPK_DESTRUCTOR_NULL;
	else if (!strcmp(argv[1], "delete"))
		cmd = WPK_DELETE;
	else
		goto wrong_usage;

	setup_and_run();

	return 0;

wrong_usage:
	usage(wpk_usage);
}

#endif /* GIT_WINDOWS_NATIVE */
