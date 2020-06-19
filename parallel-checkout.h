#ifndef PARALLEL_CHECKOUT_H
#define PARALLEL_CHECKOUT_H

struct cache_entry;
struct checkout;
struct conv_attrs;

enum pc_status {
	PC_UNINITIALIZED = 0,
	PC_ACCEPTING_ENTRIES,
	PC_RUNNING,
	PC_HANDLING_RESULTS,
};

extern enum pc_status parallel_checkout_status;

void init_parallel_checkout(struct checkout *state);

/*
 * Return -1 if parallel checkout is currently not enabled or if the entry is
 * not eligible for parallel checkout. Otherwise, enqueue the entry for later
 * write and return 0.
 */
int enqueue_checkout(struct cache_entry *ce, char *path, struct conv_attrs *ca,
		     int to_tempfile);

/*
 * Write all the queued entries, returning 0 on success. The internal parallel
 * checkout data is freed before return, making it ready for another
 * init_parallel_checkout() call. It's a bug to call this function without
 * previously initializing the parallel checkout machinery.
 */
int run_parallel_checkout(void);

#endif /* PARALLEL_CHECKOUT_H */
