#ifndef PARALLEL_CHECKOUT_H
#define PARALLEL_CHECKOUT_H

struct cache_entry;
struct checkout;
struct conv_attrs;

void init_parallel_checkout(void);

/*
 * Return -1 if parallel checkout is currently not enabled or if the entry is
 * not eligible for parallel checkout. Otherwise, enqueue the entry for later
 * write and return 0.
 */
int enqueue_checkout(struct cache_entry *ce, struct conv_attrs *ca);

/* Write all the queued entries, returning 0 on success.*/
int run_parallel_checkout(struct checkout *state);

#endif /* PARALLEL_CHECKOUT_H */
