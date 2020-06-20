#include "builtin.h"
#include "config.h"
#include "entry.h"
#include "parallel-checkout.h"
#include "parse-options.h"
#include "pkt-line.h"

static void packet_to_ci(char *line, int len, struct checkout_item *ci)
{
	struct ci_fixed_portion *fixed_portion;
	char *encoding, *variant;

	if (len < sizeof(struct ci_fixed_portion))
		BUG("checkout worker received too short item (got %d, exp %d)",
		    len, (int)sizeof(struct ci_fixed_portion));

	fixed_portion = (struct ci_fixed_portion *)line;

	if (len - sizeof(struct ci_fixed_portion) !=
		fixed_portion->name_len + fixed_portion->working_tree_encoding_len)
		BUG("checkout worker received corrupted item");

	variant = line + sizeof(struct ci_fixed_portion);
	if (fixed_portion->working_tree_encoding_len) {
		encoding = xmemdupz(variant,
				    fixed_portion->working_tree_encoding_len);
		variant += fixed_portion->working_tree_encoding_len;
	} else {
		encoding = NULL;
	}

	memset(ci, 0, sizeof(*ci));
	ci->ce = make_empty_transient_cache_entry(fixed_portion->name_len);
	ci->ce->ce_namelen = fixed_portion->name_len;
	ci->ce->ce_mode = fixed_portion->ce_mode;
	memcpy(ci->ce->name, variant, ci->ce->ce_namelen);
	oidcpy(&ci->ce->oid, &fixed_portion->oid);

	ci->id = fixed_portion->id;
	ci->ca.attr_action = fixed_portion->attr_action;
	ci->ca.crlf_action = fixed_portion->crlf_action;
	ci->ca.ident = fixed_portion->ident;
	ci->ca.working_tree_encoding = encoding;
}

static void report_result(struct checkout_item *ci)
{
	struct ci_result res = { 0 };
	size_t size;

	res.id = ci->id;
	res.status = ci->status;

	if (ci->status == CI_SUCCESS) {
		res.st = ci->st;
		size = sizeof(res);
	} else {
		size = ci_result_base_size();
	}

	packet_write(1, (const char *)&res, size);
}

/* Free the worker-side malloced data, but not the ci itself. */
static void release_checkout_item_data(struct checkout_item *ci)
{
	free((char *)ci->ca.working_tree_encoding);
	discard_cache_entry(ci->ce);
}

static void worker_loop(struct checkout *state)
{
	struct checkout_item *items = NULL;
	size_t i, nr = 0, alloc = 0;

	while (1) {
		int len;
		char *line = packet_read_line(0, &len);

		if (!line)
			break;

		ALLOC_GROW(items, nr + 1, alloc);
		packet_to_ci(line, len, &items[nr++]);
	}

	for (i = 0; i < nr; ++i) {
		struct checkout_item *ci = &items[i];
		write_checkout_item(state, ci);
		report_result(ci);
		release_checkout_item_data(ci);
	}

	packet_flush(1);

	free(items);
}

static const char * const checkout_helper_usage[] = {
	N_("git checkout--helper [<options>]"),
	NULL
};

int cmd_checkout__helper(int argc, const char **argv, const char *prefix)
{
	struct checkout state = CHECKOUT_INIT;
	struct option checkout_helper_options[] = {
		OPT_STRING(0, "prefix", &state.base_dir, N_("string"),
			N_("when creating files, prepend <string>")),
		OPT_END()
	};

	if (argc == 2 && !strcmp(argv[1], "-h"))
		usage_with_options(checkout_helper_usage,
				   checkout_helper_options);

	git_config(git_default_config, NULL);
	argc = parse_options(argc, argv, prefix, checkout_helper_options,
			     checkout_helper_usage, 0);
	if (argc > 0)
		usage_with_options(checkout_helper_usage, checkout_helper_options);

	if (state.base_dir)
		state.base_dir_len = strlen(state.base_dir);

	/*
	 * Setting this on worker won't actually update the index. We just need
	 * to pretend so to induce the checkout machinery to stat() the written
	 * entries.
	 */
	state.refresh_cache = 1;

	worker_loop(&state);
	return 0;
}
