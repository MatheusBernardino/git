#ifndef PARALLEL_CHECKOUT_H
#define PARALLEL_CHECKOUT_H

#include "entry.h"
#include "convert.h"

/****************************************************************
 * Users of parallel checkout
 ****************************************************************/

enum pc_status {
	PC_UNINITIALIZED = 0,
	PC_ACCEPTING_ENTRIES,
	PC_RUNNING,
	PC_HANDLING_RESULTS,
};

enum pc_status parallel_checkout_status(void);
void init_parallel_checkout(void);

/* Reads the checkout.workers and checkout.workersThreshold settings. */
void get_parallel_checkout_configs(int *num_workers, int *threshold);

/*
 * Return -1 if parallel checkout is currently not enabled or if the entry is
 * not eligible for parallel checkout. Otherwise, enqueue the entry for later
 * write and return 0.
 */
int enqueue_checkout(struct cache_entry *ce, struct conv_attrs *ca);

/*
 * Write all the queued entries, returning 0 on success. If the number of
 * entries is below the specified threshold, the operation is performed
 * sequentially.
 */
int run_parallel_checkout(struct checkout *state, int num_workers, int threshold);

/****************************************************************
 * Interface with checkout--helper
 ****************************************************************/

enum ci_status {
	CI_PENDING = 0,
	CI_SUCCESS,
	CI_RETRY,
	CI_FAILED,
};

struct checkout_item {
	/*
	 * In main process ce points to a istate->cache[] entry. Thus, it's not
	 * owned by us. In workers they own the memory, which *must be* released.
	 */
	struct cache_entry *ce;
	struct conv_attrs ca;
	size_t id; /* position in parallel_checkout->items[] of main process */

	/* Output fields, sent from workers. */
	enum ci_status status;
	struct stat st;
};

/*
 * The fixed-size portion of `struct checkout_item` that is sent to the workers.
 * Following this will be 2 strings: ca.working_tree_encoding and ce.name; These
 * are NOT null terminated, since we have the size in the fixed portion.
 */
struct ci_fixed_portion {
	size_t id;
	struct object_id oid;
	unsigned int ce_mode;
	enum crlf_action attr_action;
	enum crlf_action crlf_action;
	int ident;
	size_t working_tree_encoding_len;
	size_t name_len;
};

/*
 * The `struct checkout_item` fields returned by the workers. The order is
 * important here, specially stat being the last one, as it is omitted on error.
 */
struct ci_result {
	size_t id;
	enum ci_status status;
	struct stat st;
};

#define ci_result_base_size() offsetof(struct ci_result, st)

void write_checkout_item(struct checkout *state, struct checkout_item *ci);

#endif /* PARALLEL_CHECKOUT_H */
