#include "cache.h"
#include "entry.h"
#include "parallel-checkout.h"
#include "streaming.h"

enum ci_status {
	CI_PENDING = 0,
	CI_SUCCESS,
	CI_RETRY,
	CI_FAILED,
};

struct checkout_item {
	/* pointer to a istate->cache[] entry. Not owned by us. */
	struct cache_entry *ce;
	struct conv_attrs ca;
	struct stat st;
	enum ci_status status;
};

struct parallel_checkout {
	struct checkout_item *items;
	size_t nr, alloc;
};

static struct parallel_checkout *parallel_checkout = NULL;

enum pc_status {
	PC_UNINITIALIZED = 0,
	PC_ACCEPTING_ENTRIES,
	PC_RUNNING,
	PC_HANDLING_RESULTS,
};

static enum pc_status pc_status = PC_UNINITIALIZED;

void init_parallel_checkout(void)
{
	if (parallel_checkout)
		BUG("parallel checkout already initialized");

	parallel_checkout = xcalloc(1, sizeof(*parallel_checkout));
	pc_status = PC_ACCEPTING_ENTRIES;
}

static void finish_parallel_checkout(void)
{
	if (!parallel_checkout)
		BUG("cannot finish parallel checkout: not initialized yet");

	free(parallel_checkout->items);
	FREE_AND_NULL(parallel_checkout);
	pc_status = PC_UNINITIALIZED;
}

static int is_eligible_for_parallel_checkout(const struct cache_entry *ce,
					     const struct conv_attrs *ca)
{
	enum conv_attrs_classification c;

	if (!S_ISREG(ce->ce_mode))
		return 0;

	c = classify_conv_attrs(ca);
	switch (c) {
	case CA_CLASS_INCORE:
		return 1;

	case CA_CLASS_INCORE_FILTER:
		/*
		 * It would be safe to allow concurrent instances of
		 * single-file smudge filters, like rot13, but we should not
		 * assume that all filters are parallel-process safe. So we
		 * don't allow this.
		 */
		return 0;

	case CA_CLASS_INCORE_PROCESS:
		/*
		 * The parallel queue and the delayed queue are not compatible,
		 * so they must be kept completely separated. And we can't tell
		 * if a long-running process will delay its response without
		 * actually asking it to perform the filtering. Therefore, this
		 * type of filter is not allowed in parallel checkout.
		 *
		 * Furthermore, there should only be one instance of the
		 * long-running process filter as we don't know how it is
		 * managing its own concurrency. So, spreading the entries that
		 * requisite such a filter among the parallel workers would
		 * require a lot more inter-process communication. We would
		 * probably have to designate a single process to interact with
		 * the filter and send all the necessary data to it, for each
		 * entry.
		 */
		return 0;

	case CA_CLASS_STREAMABLE:
		return 1;

	default:
		BUG("unsupported conv_attrs classification '%d'", c);
	}
}

int enqueue_checkout(struct cache_entry *ce, struct conv_attrs *ca)
{
	struct checkout_item *ci;

	if (!parallel_checkout || pc_status != PC_ACCEPTING_ENTRIES ||
	    !is_eligible_for_parallel_checkout(ce, ca))
		return -1;

	ALLOC_GROW(parallel_checkout->items, parallel_checkout->nr + 1,
		   parallel_checkout->alloc);

	ci = &parallel_checkout->items[parallel_checkout->nr++];
	ci->ce = ce;
	memcpy(&ci->ca, ca, sizeof(ci->ca));

	return 0;
}

static int handle_results(struct checkout *state)
{
	int ret = 0;
	size_t i;

	pc_status = PC_HANDLING_RESULTS;

	for (i = 0; i < parallel_checkout->nr; ++i) {
		struct checkout_item *ci = &parallel_checkout->items[i];
		struct stat *st = &ci->st;

		switch(ci->status) {
		case CI_SUCCESS:
			update_ce_after_write(state, ci->ce, st);
			break;
		case CI_RETRY:
			/*
			 * The fails for which we set CI_RETRY are the ones
			 * that might have been caused by a path collision. So
			 * we let checkout_entry_ca() retry writing, as it will
			 * properly handle collisions and the creation of
			 * leading dirs in the entry's path.
			 */
			ret |= checkout_entry_ca(ci->ce, &ci->ca, state, NULL, NULL);
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

static int reset_fd(int fd, const char *path)
{
	if (lseek(fd, 0, SEEK_SET) != 0)
		return error_errno("failed to rewind descriptor of %s", path);
	if (ftruncate(fd, 0))
		return error_errno("failed to truncate file %s", path);
	return 0;
}

static int write_checkout_item_to_fd(int fd, struct checkout *state,
				     struct checkout_item *ci, const char *path)
{
	int ret;
	struct stream_filter *filter;
	struct strbuf buf = STRBUF_INIT;
	char *new_blob;
	unsigned long size;
	size_t newsize = 0;
	ssize_t wrote;

	/* Sanity check */
	assert(is_eligible_for_parallel_checkout(ci->ce, &ci->ca));

	filter = get_stream_filter_ca(&ci->ca, &ci->ce->oid);
	if (filter) {
		if (stream_blob_to_fd(fd, &ci->ce->oid, filter, 1)) {
			/* On error, reset fd to try writing without streaming */
			if (reset_fd(fd, path))
				return -1;
		} else {
			return 0;
		}
	}

	new_blob = read_blob_entry(ci->ce, &size);
	if (!new_blob)
		return error("unable to read sha1 file of %s (%s)", path,
			     oid_to_hex(&ci->ce->oid));

	/*
	 * checkout metadata is used to give context for external process
	 * filters. Files requiring such filters are not eligible for parallel
	 * checkout, so pass NULL.
	 */
	ret = convert_to_working_tree_ca(&ci->ca, ci->ce->name, new_blob, size,
					 &buf, NULL);

	if (ret) {
		free(new_blob);
		new_blob = strbuf_detach(&buf, &newsize);
		size = newsize;
	}

	wrote = write_in_full(fd, new_blob, size);
	free(new_blob);
	if (wrote < 0)
		return error("unable to write file %s", path);

	return 0;
}

static int close_and_clear(int *fd)
{
	int ret = 0;

	if (*fd >= 0) {
		ret = close(*fd);
		*fd = -1;
	}

	return ret;
}

static int check_leading_dirs(const char *path, int len, int prefix_len)
{
	const char *slash = path + len;

	while (slash > path && *slash != '/')
		slash--;

	return has_dirs_only_path(path, slash - path, prefix_len);
}

static void write_checkout_item(struct checkout *state, struct checkout_item *ci)
{
	unsigned int mode = (ci->ce->ce_mode & 0100) ? 0777 : 0666;
	int fd = -1, fstat_done = 0;
	struct strbuf path = STRBUF_INIT;

	strbuf_add(&path, state->base_dir, state->base_dir_len);
	strbuf_add(&path, ci->ce->name, ce_namelen(ci->ce));

	/*
	 * At this point, leading dirs should have already been created. But if
	 * a symlink being checked out has collided with one of the dirs, due to
	 * file system folding rules, it's possible that the dirs are no longer
	 * present. So we have to check again, and report any path collisions.
	 */
	if (!check_leading_dirs(path.buf, path.len, state->base_dir_len)) {
		ci->status = CI_RETRY;
		goto out;
	}

	fd = open(path.buf, O_WRONLY | O_CREAT | O_EXCL, mode);

	if (fd < 0) {
		if (errno == EEXIST || errno == EISDIR) {
			/*
			 * Errors which probably represent a path collision.
			 * Suppress the error message and mark the ci to be
			 * retried later, sequentially. ENOTDIR and ENOENT are
			 * also interesting, but check_leading_dirs() should
			 * have already caught these cases.
			 */
			ci->status = CI_RETRY;
		} else {
			error_errno("failed to open file %s", path.buf);
			ci->status = CI_FAILED;
		}
		goto out;
	}

	if (write_checkout_item_to_fd(fd, state, ci, path.buf)) {
		/* Error was already reported. */
		ci->status = CI_FAILED;
		goto out;
	}

	fstat_done = fstat_checkout_output(fd, state, &ci->st);

	if (close_and_clear(&fd)) {
		error_errno("unable to close file %s", path.buf);
		ci->status = CI_FAILED;
		goto out;
	}

	if (state->refresh_cache && !fstat_done && lstat(path.buf, &ci->st) < 0) {
		error_errno("unable to stat just-written file %s",  path.buf);
		ci->status = CI_FAILED;
		goto out;
	}

	ci->status = CI_SUCCESS;

out:
	/*
	 * No need to check close() return. At this point, either fd is already
	 * closed, or we are on an error path, that has already been reported.
	 */
	close_and_clear(&fd);
	strbuf_release(&path);
}

static int run_checkout_sequentially(struct checkout *state)
{
	size_t i;

	for (i = 0; i < parallel_checkout->nr; ++i) {
		struct checkout_item *ci = &parallel_checkout->items[i];
		write_checkout_item(state, ci);
	}

	return handle_results(state);
}


int run_parallel_checkout(struct checkout *state)
{
	int ret;

	if (!parallel_checkout)
		BUG("cannot run parallel checkout: not initialized yet");

	pc_status = PC_RUNNING;

	ret = run_checkout_sequentially(state);

	finish_parallel_checkout();
	return ret;
}
