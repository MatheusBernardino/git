#include "cache.h"
#include "entry.h"
#include "parallel-checkout.h"
#include "pkt-line.h"
#include "run-command.h"
#include "streaming.h"
#include "thread-utils.h"
#include "config.h"

struct parallel_checkout {
	struct checkout_item *items;
	size_t nr, alloc;
};

static struct parallel_checkout *parallel_checkout = NULL;
static enum pc_status pc_status = PC_UNINITIALIZED;

enum pc_status parallel_checkout_status(void)
{
	return pc_status;
}

#define DEFAULT_WORKERS_THRESHOLD 0

void get_parallel_checkout_configs(int *num_workers, int *threshold)
{
	if (git_config_get_int("checkout.workers", num_workers) ||
	    *num_workers < 1)
		*num_workers = online_cpus();

	if (git_config_get_int("checkout.workersThreshold", threshold) ||
	    *threshold < 0)
		*threshold = DEFAULT_WORKERS_THRESHOLD;
}

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

	ci = &parallel_checkout->items[parallel_checkout->nr];
	ci->ce = ce;
	memcpy(&ci->ca, ca, sizeof(ci->ca));
	ci->id = parallel_checkout->nr;
	parallel_checkout->nr++;

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
	 * checkout, so pass NULL. Note: if that changes, the metadata must also
	 * be passed from the main process to the workers.
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

void write_checkout_item(struct checkout *state, struct checkout_item *ci)
{
	unsigned int mode = (ci->ce->ce_mode & 0100) ? 0777 : 0666;
	int fd = -1, fstat_done = 0;
	struct strbuf path = STRBUF_INIT;

	strbuf_add(&path, state->base_dir, state->base_dir_len);
	strbuf_add(&path, ci->ce->name, ci->ce->ce_namelen);

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

static void send_one_item(int fd, struct checkout_item *ci)
{
	size_t len_data;
	char *data, *variant;
	struct ci_fixed_portion *fixed_portion;
	const char *working_tree_encoding = ci->ca.working_tree_encoding;
	size_t name_len = ci->ce->ce_namelen;
	size_t working_tree_encoding_len = working_tree_encoding ?
					   strlen(working_tree_encoding) : 0;

	len_data = sizeof(struct ci_fixed_portion) + name_len +
		   working_tree_encoding_len;

	data = xcalloc(1, len_data);

	fixed_portion = (struct ci_fixed_portion *)data;
	fixed_portion->id = ci->id;
	oidcpy(&fixed_portion->oid, &ci->ce->oid);
	fixed_portion->ce_mode = ci->ce->ce_mode;
	fixed_portion->attr_action = ci->ca.attr_action;
	fixed_portion->crlf_action = ci->ca.crlf_action;
	fixed_portion->ident = ci->ca.ident;
	fixed_portion->name_len = name_len;
	fixed_portion->working_tree_encoding_len = working_tree_encoding_len;

	variant = data + sizeof(*fixed_portion);
	if (working_tree_encoding_len) {
		memcpy(variant, working_tree_encoding, working_tree_encoding_len);
		variant += working_tree_encoding_len;
	}
	memcpy(variant, ci->ce->name, name_len);

	packet_write(fd, data, len_data);

	free(data);
}

static void send_batch(int fd, size_t start, size_t nr)
{
	size_t i;
	for (i = 0; i < nr; ++i)
		send_one_item(fd, &parallel_checkout->items[start + i]);
	packet_flush(fd);
}

static struct child_process *setup_workers(struct checkout *state, int num_workers)
{
	struct child_process *workers;
	int i, workers_with_one_extra_item;
	size_t base_batch_size, next_to_assign = 0;

	base_batch_size = parallel_checkout->nr / num_workers;
	workers_with_one_extra_item = parallel_checkout->nr % num_workers;
	ALLOC_ARRAY(workers, num_workers);

	for (i = 0; i < num_workers; ++i) {
		struct child_process *cp = &workers[i];
		size_t batch_size = base_batch_size;

		child_process_init(cp);
		cp->git_cmd = 1;
		cp->in = -1;
		cp->out = -1;
		strvec_push(&cp->args, "checkout--helper");
		if (state->base_dir_len)
			strvec_pushf(&cp->args, "--prefix=%s", state->base_dir);
		if (start_command(cp))
			die(_("failed to spawn checkout worker"));

		/* distribute the extra work evenly */
		if (i < workers_with_one_extra_item)
			batch_size++;

		send_batch(cp->in, next_to_assign, batch_size);
		next_to_assign += batch_size;
	}

	return workers;
}

static void finish_workers(struct child_process *workers, int num_workers)
{
	int i;
	for (i = 0; i < num_workers; ++i) {
		struct child_process *w = &workers[i];
		if (w->in >= 0)
			close(w->in);
		if (w->out >= 0)
			close(w->out);
		if (finish_command(w))
			die(_("checkout worker finished with error"));
	}
	free(workers);
}

static void parse_and_save_result(const char *line, int len)
{
	struct ci_result *res;
	struct checkout_item *ci;

	/*
	 * Worker should send either the full result struct or just the base
	 * (i.e. no stat data).
	 */
	if (len != ci_result_base_size() && len != sizeof(struct ci_result))
		BUG("received corrupted item from checkout worker");

	res = (struct ci_result *)line;

	if (res->id > parallel_checkout->nr)
		BUG("checkout worker sent unknown item id");

	ci = &parallel_checkout->items[res->id];
	ci->status = res->status;

	/*
	 * Worker only sends stat data on success. Otherwise, we *cannot* access
	 * res->st as that will be an invalid address.
	 */
	if (res->status == CI_SUCCESS)
		ci->st = res->st;
}

static void gather_results_from_workers(struct child_process *workers,
					int num_workers)
{
	int i, active_workers = num_workers;
	struct pollfd *pfds;

	CALLOC_ARRAY(pfds, num_workers);
	for (i = 0; i < num_workers; ++i) {
		pfds[i].fd = workers[i].out;
		pfds[i].events = POLLIN;
	}

	while (active_workers) {
		int nr = poll(pfds, num_workers, -1);

		if (nr < 0) {
			if (errno == EINTR)
				continue;
			die_errno("failed to poll checkout workers");
		}

		for (i = 0; i < num_workers && nr > 0; ++i) {
			struct pollfd *pfd = &pfds[i];

			if (!pfd->revents)
				continue;

			if (pfd->revents & POLLIN) {
				int len;
				const char *line = packet_read_line(pfd->fd, &len);

				if (!line) {
					pfd->fd = -1;
					active_workers--;
				} else {
					parse_and_save_result(line, len);
				}
			} else if (pfd->revents & POLLHUP) {
				pfd->fd = -1;
				active_workers--;
			} else if (pfd->revents & (POLLNVAL | POLLERR)) {
				die(_("error polling from checkout worker"));
			}

			nr--;
		}
	}

	free(pfds);
}

static int run_checkout_sequentially(struct checkout *state)
{
	size_t i;
	for (i = 0; i < parallel_checkout->nr; ++i)
		write_checkout_item(state, &parallel_checkout->items[i]);
	return handle_results(state);
}

int run_parallel_checkout(struct checkout *state, int num_workers, int threshold)
{
	int ret = 0;
	struct child_process *workers;

	if (!parallel_checkout)
		BUG("cannot run parallel checkout: not initialized yet");

	if (num_workers < 1)
		BUG("invalid number of workers for run_parallel_checkout: %d",
		    num_workers);

	pc_status = PC_RUNNING;

	if (parallel_checkout->nr == 0) {
		goto done;
	} else if (parallel_checkout->nr < threshold || num_workers == 1) {
		ret = run_checkout_sequentially(state);
		goto done;
	}

	workers = setup_workers(state, num_workers);
	gather_results_from_workers(workers, num_workers);
	finish_workers(workers, num_workers);
	ret = handle_results(state);

done:
	finish_parallel_checkout();
	return ret;
}
