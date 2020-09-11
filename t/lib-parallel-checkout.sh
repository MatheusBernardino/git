# Helpers for t208* tests

# Parallel checkout tests need full control of the number of workers
unset GIT_TEST_CHECKOUT_WORKERS

parallel_checkout_config () {
	test_config_global checkout.workers $1 &&
	test_config_global checkout.thresholdForParallelism $2
}

test_workers_in_trace () {
	test $(grep "child_start\[..*\] git checkout--worker" "$1" | wc -l) -eq "$2"
}

test_workers_in_event_trace () {
	test $(grep ".event.:.child_start..*checkout--worker" "$1" | wc -l) -eq "$2"
}

# Verify that both the working tree and the index were created correctly
verify_checkout () {
	git -C "$1" diff-index --quiet HEAD -- &&
	git -C "$1" diff-index --quiet --cached HEAD -- &&
	git -C "$1" status --porcelain >"$1".status &&
	test_must_be_empty "$1".status
}
