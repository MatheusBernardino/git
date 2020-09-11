# Helpers for t208* tests

# Parallel checkout tests need full control of the number of workers
unset GIT_TEST_CHECKOUT_WORKERS

set_checkout_config () {
	if test $# -ne 2
	then
		BUG "set_checkout_config() requires two arguments"
	fi &&

	test_config_global checkout.workers $1 &&
	test_config_global checkout.thresholdForParallelism $2
}

# Run "${@:2}" and check that $1 checkout workers were used
test_checkout_workers () {
	if test $# -lt 2
	then
		BUG "too few arguments to test_checkout_workers()"
	fi &&

	expected_workers=$1 &&
	shift &&

	rm -f trace &&
	GIT_TRACE2="$(pwd)/trace" "$@" 2>&8 &&

	workers=$(grep "child_start\[..*\] git checkout--worker" trace | wc -l) &&
	test $workers -eq $expected_workers &&
	rm -f trace
} 8>&2 2>&4

# Verify that both the working tree and the index were created correctly
verify_checkout () {
	git -C "$1" diff-index --quiet HEAD -- &&
	git -C "$1" diff-index --quiet --cached HEAD -- &&
	git -C "$1" status --porcelain >"$1".status &&
	test_must_be_empty "$1".status
}
