#!/bin/sh

test_description='parallel-checkout basics

Ensure that parallel-checkout basically works on a clone, spawning the required
number of workers and correctly populating both the index and working tree.
'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh

R_BASE=$GIT_BUILD_DIR

check_clone() {
	id=$1 workers=$2 threshold=$3 expected_workers=$4 &&

	GIT_TRACE2="$(pwd)/$id.trace" git \
		-c checkout.workers=$workers \
		-c checkout.thresholdForParallelism=$threshold \
		clone -- $R_BASE r_$id &&

	# Check that the expected number of workers was used. Note that it might
	# be different than the requested number due to the threshold value.
	local workers_in_trace=$(grep "child_start\[.\] git checkout--helper" $id.trace | wc -l) &&
	test $workers_in_trace -eq $expected_workers &&

	# Verify that both the working tree and the index were created correctly
	git -C r_$id diff-index --quiet HEAD -- &&
	git -C r_$id diff-index --quiet --cached HEAD -- &&
	git -C r_$id status --porcelain >$id.status &&
	test_must_be_empty $id.status
}

test_expect_success 'sequential clone' '
	check_clone sequential 1 0 0
'

test_expect_success 'parallel clone' '
	check_clone parallel 2 0 2
'

test_expect_success 'fallback to sequential clone (threshold)' '
	git -C $R_BASE ls-files >files &&
	nr_files=$(wc -l <files) &&
	threshold=$(($nr_files + 1)) &&
	check_clone sequential_fallback 2 $threshold 0
'

# Just to be paranoid, actually compare the contents of the worktrees directly.
test_expect_success 'compare working trees from clones' '
	rm -rf r_sequential/.git &&
	rm -rf r_parallel/.git &&
	rm -rf r_sequential_fallback/.git &&
	diff -qr r_sequential r_parallel &&
	diff -qr r_sequential r_sequential_fallback
'

test_done
