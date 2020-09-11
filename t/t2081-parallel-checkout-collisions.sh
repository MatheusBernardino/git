#!/bin/sh

test_description='parallel-checkout collisions'

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-parallel-checkout.sh"

# When there are pathname collisions during a clone, Git should report a warning
# listing all of the colliding entries. The sequential code detects a collision
# by calling lstat() before trying to open(O_CREAT) the file. Then, to find the
# colliding pair of an item k, it searches cache_entry[0, k-1].
#
# This is not sufficient in parallel-checkout mode since colliding files may be
# created in a racy order. The tests in this file make sure the collision
# detection code is extended for parallel-checkout. This is done in two parts:
#
# - First, two parallel workers create four colliding files racily.
# - Then this exercise is repeated but forcing the colliding pair to appear in
#   the second half of the cache_entry's array.
#
# The second item uses the fact that files with clean/smudge filters are not
# parallel-eligible; and that they are processed sequentially *before* any
# worker is spawned. We set a filter attribute to the last entry in the
# cache_entry[] array, making it non-eligible, so that it is populated first.
# This way, we can test if the collision detection code is correctly looking
# for collision pairs in the second half of the array.

test_expect_success CASE_INSENSITIVE_FS 'setup' '
	file_hex=$(git hash-object -w --stdin </dev/null) &&
	file_oct=$(echo $file_hex | hex2oct) &&

	attr_hex=$(echo "file_x filter=logger" | git hash-object -w --stdin) &&
	attr_oct=$(echo $attr_hex | hex2oct) &&

	printf "100644 FILE_X\0${file_oct}" >tree &&
	printf "100644 FILE_x\0${file_oct}" >>tree &&
	printf "100644 file_X\0${file_oct}" >>tree &&
	printf "100644 file_x\0${file_oct}" >>tree &&
	printf "100644 .gitattributes\0${attr_oct}" >>tree &&

	tree_hex=$(git hash-object -w -t tree --stdin <tree) &&
	commit_hex=$(git commit-tree -m collisions $tree_hex) &&
	git update-ref refs/heads/collisions $commit_hex &&

	write_script logger_script <<-\EOF
	echo "$@" >>filter.log
	EOF
'

clone_and_check_collision()
{
	id=$1 workers=$2 threshold=$3 expected_workers=$4 filter=$5 &&

	filter_opts=
	if test "$filter" -eq "use_filter"
	then
		# We use `core.ignoreCase=0` so that only `file_x`
		# matches the pattern in .gitattributes.
		#
		filter_opts='-c filter.logger.smudge="../logger_script %f" -c core.ignoreCase=0'
	fi &&

	test_path_is_missing $id.trace &&
	GIT_TRACE2="$(pwd)/$id.trace" git \
		-c checkout.workers=$workers \
		-c checkout.thresholdForParallelism=$threshold \
		$filter_opts clone --branch=collisions -- . r_$id 2>$id.warning &&

	# Check that checkout spawned the right number of workers
	workers_in_trace=$(grep "child_start\[.\] git checkout--helper" $id.trace | wc -l) &&
	test $workers_in_trace -eq $expected_workers &&

	if test $filter -eq "use_filter"
	then
		#  Make sure only 'file_x' was filtered
		test_path_is_file r_$id/filter.log &&
		echo file_x >expected.filter.log &&
		test_cmp r_$id/filter.log expected.filter.log
	else
		test_path_is_missing r_$id/filter.log
	fi &&

	grep FILE_X $id.warning &&
	grep FILE_x $id.warning &&
	grep file_X $id.warning &&
	grep file_x $id.warning &&
	test_i18ngrep "the following paths have collided" $id.warning
}

test_expect_success CASE_INSENSITIVE_FS 'collision detection on parallel clone' '
	clone_and_check_collision parallel 2 0 2
'

test_expect_success CASE_INSENSITIVE_FS 'collision detection on fallback to sequential clone' '
	git ls-tree --name-only -r collisions >files &&
	nr_files=$(wc -l <files) &&
	threshold=$(($nr_files + 1)) &&
	clone_and_check_collision sequential 2 $threshold 0
'

# The next two tests don't work on Windows because, on this system, collision
# detection uses strcmp() (when core.ignoreCase=0) to find the colliding pair.
# But they work on OSX, where collision detection uses inode.

test_expect_success CASE_INSENSITIVE_FS,!MINGW,!CYGWIN 'collision detection on parallel clone w/ filter' '
	clone_and_check_collision parallel-with-filter 2 0 2 use_filter
'

test_expect_success CASE_INSENSITIVE_FS,!MINGW,!CYGWIN 'collision detection on fallback to sequential clone w/ filter' '
	git ls-tree --name-only -r collisions >files &&
	nr_files=$(wc -l <files) &&
	threshold=$(($nr_files + 1)) &&
	clone_and_check_collision sequential-with-filter 2 $threshold 0 use_filter
'

test_done
