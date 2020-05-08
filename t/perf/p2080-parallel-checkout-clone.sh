#!/bin/sh

test_description='performance test for parallel-checkout'

. ./perf-lib.sh

# The path to the borrowed source repo will be in $GIT_PERF_BORROW_REPO
# We must treat it as read-only.
#
test_perf_borrow_large_repo

# We create an instance of the repository in each `test_perf`, but
# to allow `./run` to work, we need to delete them inside the
# `test_perf` block, so that time gets added to the test.
#
# TODO Is there a way to get around that?
#
#
# Each clone command inclues `| cat` to eat the exit code.
# This is avoid long-pathname problems on Windows.  (Every
# large third-party repo I tested seemed to have a few deeply
# nested files that are too long when appended to the trash
# name directory.)
#
# TODO Is there a better way to handle this?  This was a problem
# even when using the `--root` option (which helps, but not
# enough).

for ths in 1 2 4 8
do
	export ths

	test_perf "clone with ${ths} threads" '
		r_out=./r_${ths}ths &&

		git -c checkout.threads=${ths} \
		    -c checkout.minLimitForThreads=0 \
			clone -- $GIT_PERF_BORROW_REPO $r_out | cat &&
		rm -rf $r_out
	'
done

test_done
