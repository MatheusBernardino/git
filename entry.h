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

#endif /* ENTRY_H */
