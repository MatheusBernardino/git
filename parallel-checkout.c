#include "cache.h"
#include "entry.h"
#include "object-store.h"
#include "parallel-checkout.h"

enum ci_status {
	CI_PENDING = 0,
	CI_SUCCESS,
	CI_RETRY,
	CI_FAILED,
};

struct checkout_item {
	struct checkout_item *next;
	/* pointer to a istate->cache[] entry. Not owned by us. */
	struct cache_entry *ce;
	char *path;
	struct conv_attrs ca;
	int to_tempfile;
	struct stat st_out;
	enum ci_status status;
};

struct parallel_checkout {
	struct checkout *state;
	struct checkout_item *items;
	size_t nr, alloc;
};

static struct parallel_checkout *parallel_checkout = NULL;
enum pc_status parallel_checkout_status;

void init_parallel_checkout(struct checkout *state)
{
	if (parallel_checkout)
		BUG("parallel checkout already initialized");

	parallel_checkout = xcalloc(1, sizeof(*parallel_checkout));
	parallel_checkout->state = state;
	parallel_checkout_status = PC_ACCEPTING_ENTRIES;
}

static void finish_parallel_checkout(void)
{
	size_t i;

	if (!parallel_checkout)
		BUG("cannot finish parallel checkout: not initialized yet");

	for (i = 0; i < parallel_checkout->nr; ++i)
		free(parallel_checkout->items[i].path);

	free(parallel_checkout->items);
	FREE_AND_NULL(parallel_checkout);
	parallel_checkout_status = PC_UNINITIALIZED;
}

static int is_eligible_for_parallel_checkout(const struct cache_entry *ce,
					     const struct conv_attrs *ca)
{
	enum conv_attrs_classification c;

	/* The submodule functions in write_entry() are not thread-safe. */
	if (S_ISGITLINK(ce->ce_mode))
		return 0;

	c = classify_conv_attrs(ca);
	switch (c) {
	default:
		BUG("unsupported conv_attrs classification '%d'", c);

	case CA_CLASS_INCORE:
		return 1;

	case CA_CLASS_INCORE_FILTER:
		/*
		 * Running an external process filter is not thread-safe yet
		 * (the async machinery expects to be executed by the main
		 * thread only).
		 *
		 * Also, it would be safe to allow concurrent instances of
		 * single-file smudge filters, like rot13, but we should not
		 * assume that all filters are parallel-process safe.
		 */
		return 0;

	case CA_CLASS_INCORE_PROCESS:
		/*
		 * The same reasons for CA_CLASS_INCORE_FILTER apply here: the
		 * async machinery cannot run threaded yet. Besides, the
		 * parallel queue and the delayed queue are not compatible and
		 * must be kept completely separate.
		 */
		return 0;

	case CA_CLASS_STREAMABLE:
		return 1;
	}
}

int enqueue_checkout(struct cache_entry *ce, char *path, struct conv_attrs *ca,
		     int to_tempfile)
{
	struct checkout_item *ci;

	if (!parallel_checkout || parallel_checkout_status != PC_ACCEPTING_ENTRIES ||
	    !is_eligible_for_parallel_checkout(ce, ca))
		return -1;

	ALLOC_GROW(parallel_checkout->items, parallel_checkout->nr + 1,
		   parallel_checkout->alloc);

	ci = &parallel_checkout->items[parallel_checkout->nr++];
	ci->ce = ce;
	ci->path = strdup(path);
	memcpy(&ci->ca, ca, sizeof(ci->ca));
	ci->to_tempfile = to_tempfile;

	return 0;
}

static int handle_results(void)
{
	int ret = 0;
	size_t i;
	struct checkout *state = parallel_checkout->state;

	parallel_checkout_status = PC_HANDLING_RESULTS;

	for (i = 0; i < parallel_checkout->nr; ++i) {
		struct checkout_item *ci = &parallel_checkout->items[i];
		struct stat *st = &ci->st_out;

		switch(ci->status) {
		case CI_SUCCESS:
			update_ce_after_write(state, ci->ce, st);
			break;
		case CI_RETRY:
			/*
			 * The fails for which we set CI_RETRY are the ones
			 * that might have been caused by a path collision. So
			 * we let checkout_entry() retry writing, as it will
			 * properly handle collisions and the creation of
			 * leading dirs in the entry's path.
			 */
			ret |= checkout_entry(ci->ce, state,
					ci->to_tempfile ? ci->path : NULL, NULL);
			break;
		case CI_FAILED:
			ret = -1;
			break;
		case CI_PENDING:
			BUG("parallel checkout finished with pending entries");
		default:
			BUG("unknown checkout item status in parallel checkout");
		}
	}

	return ret;
}

/*
 * Returns true if write_entry() is likely to have failed due to a path
 * collision (e.g. case-sensitive files in case-insensitive file systems). A
 * false positive is better than a false negative, here! (Although too much
 * false negatives is bad too, as it would generate unecessary duplicated work.)
 *
 * Note: we look for ENOENT and ENOTDIR because checkout_entry() should have
 * created the leading directories for the entry. Thus, if the failure was due
 * to the inexistence of such dirs, it should have been a collision (that
 * happened even before spawning the threads).
 */
#define looks_like_collision_error(we_error, errnum) \
	((we_error == WE_OPEN_ERROR || we_error == WE_SYMLINK_ERROR) && \
	 (errnum == EEXIST || errnum == ENOENT || errnum == ENOTDIR))

static int run_checkout_sequentially(void)
{
	size_t i;
	struct checkout *state = parallel_checkout->state;

	for (i = 0; i < parallel_checkout->nr; ++i) {
		struct checkout_item *ci = &parallel_checkout->items[i];
		struct stat *st = &ci->st_out;

		int err = write_entry(ci->ce, ci->path, &ci->ca, state,
				      ci->to_tempfile, st);

		if (err == WE_SUCCESS)
			ci->status = CI_SUCCESS;
		else if (looks_like_collision_error(err, errno))
			ci->status = CI_RETRY;
		else
			ci->status = CI_FAILED;
	}

	return handle_results();
}


int run_parallel_checkout(void)
{
	int err;

	if (!parallel_checkout)
		BUG("cannot run parallel checkout: not initialized yet");

	parallel_checkout_status = PC_RUNNING;

	err = run_checkout_sequentially();

	finish_parallel_checkout();
	return err;
}
