#ifndef ENTRY_H
#define ENTRY_H

#include "cache.h"
#include "convert.h"

struct checkout {
	struct index_state *istate;
	const char *base_dir;
	int base_dir_len;
	struct delayed_checkout *delayed_checkout;
	struct checkout_metadata meta;
	unsigned force:1,
		 quiet:1,
		 not_new:1,
		 clone:1,
		 refresh_cache:1;
};
#define CHECKOUT_INIT { NULL, "" }

#define TEMPORARY_FILENAME_LENGTH 25
int checkout_entry(struct cache_entry *ce, const struct checkout *state, char *topath, int *nr_checkouts);
void enable_delayed_checkout(struct checkout *state);
int finish_delayed_checkout(struct checkout *state, int *nr_checkouts);
/*
 * Unlink the last component and schedule the leading directories for
 * removal, such that empty directories get removed.
 */
void unlink_entry(const struct cache_entry *ce);

#define WE_SUCCESS 0
#define WE_GENERIC_ERROR -1
#define WE_OPEN_ERROR -2
#define WE_SYMLINK_ERROR -3

/*
 * NOTE: write_entry() and update_ce_after_write() are public only to be used
 * in parallel-checkout.c. Others should call checkout_entry(), instead, as it
 * handles path collisions, creates missing dirs, etc.
 */
/*
 * On success, return 0 and save the stat info of the just-written file in
 * st_out. Otherwise, an error code is returned. On errors other than
 * WE_GENERIC_ERROR, errno will contain the error cause. Note: ca is required
 * iff the entry refers to a regular file.
 */
int write_entry(struct cache_entry *ce, char *path, struct conv_attrs *ca,
		const struct checkout *state, int to_tempfile,
		struct stat *st_out);
void update_ce_after_write(const struct checkout *state, struct cache_entry *ce,
			   struct stat *st);

#endif /* ENTRY_H */
