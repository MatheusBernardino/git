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
};

enum pc_status parallel_checkout_status(void);
void get_parallel_checkout_configs(int *num_workers, int *threshold);
void init_parallel_checkout(void);

/*
 * Return -1 if parallel checkout is currently not enabled or if the entry is
 * not eligible for parallel checkout. Otherwise, enqueue the entry for later
 * write and return 0.
 */
int enqueue_checkout(struct cache_entry *ce, struct conv_attrs *ca);
size_t pc_queue_size(void);

/*
 * Enqueues a symlink to be checked out *sequentially* after the parallel
 * checkout finishes. This is done to avoid path collisions with leading dirs,
 * which could make parallel workers write a file to the wrong place.
 */
void enqueue_symlink_checkout(struct cache_entry *ce, int *nr_checkouts);
size_t symlink_queue_size(void);

/*
 * Write all entries from the parallel-checkout queue and the symlink queue,
 * returning 0 on success. If the number of entries in the parallel checkout
 * queue is smaller than the specified threshold, the operation is performed
 * sequentially.
 */
int run_parallel_checkout(struct checkout *state, int num_workers, int threshold,
			  struct progress *progress, unsigned int *progress_cnt);

/****************************************************************
 * Interface with checkout--helper
 ****************************************************************/

enum pc_item_status {
	PC_ITEM_PENDING = 0,
	PC_ITEM_WRITTEN,
	/*
	 * The entry could not be written because there was another file
	 * already present in its path or leading directories. Since the main
	 * process removes such files before spawning the workers, it means
	 * that there was a path collision among the entries being checked out.
	 */
	PC_ITEM_COLLIDED,
	PC_ITEM_FAILED,
};

struct parallel_checkout_item {
	/*
	 * In main process ce points to a istate->cache[] entry. Thus, it's not
	 * owned by us. In workers they own the memory, which *must be* released.
	 */
	struct cache_entry *ce;
	struct conv_attrs ca;
	size_t id; /* position in parallel_checkout.items[] of main process */

	/* Output fields, sent from workers. */
	enum pc_item_status status;
	struct stat st;
};

/*
 * The fixed-size portion of `struct parallel_checkout_item` that is sent to the
 * workers. Following this will be 2 strings: ca.working_tree_encoding and
 * ce.name; These are NOT null terminated, since we have the size in the fixed
 * portion.
 */
struct pc_item_fixed_portion {
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
 * The fields of `struct parallel_checkout_item` that are returned by the
 * workers. Note: `st` must be the last one, as it is omitted on error.
 */
struct pc_item_result {
	size_t id;
	enum pc_item_status status;
	struct stat st;
};

#define PC_ITEM_RESULT_BASE_SIZE offsetof(struct pc_item_result, st)

void write_pc_item(struct parallel_checkout_item *pc_item,
		   struct checkout *state);

#endif /* PARALLEL_CHECKOUT_H */
