#!/bin/sh

test_description='parallel-checkout basics

Ensure that parallel-checkout basically works on clone and checkout, spawning
the required number of workers and correctly populating both the index and the
working tree.
'

TEST_NO_CREATE_REPO=1
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-parallel-checkout.sh"

# Test parallel-checkout with a branch switch containing file creations,
# deletions, and modification; with different entry types. Switching from B1 to
# B2 will have the following changes:
#
# - a (file):      modified
# - e/x (file):    deleted
# - b (symlink):   deleted
# - b/f (file):    created
# - e (symlink):   created
# - d (submodule): created
#
test_expect_success SYMLINKS 'setup repo for checkout with various types of changes' '
	git init submodule &&
	test_commit -C submodule f &&

	git init various &&
	(
		cd various &&
		git checkout -b B1 &&
		echo a >a &&
		mkdir e &&
		echo e/x >e/x &&
		ln -s e b &&
		git add -A &&
		git commit -m B1 &&

		git checkout -b B2 &&
		echo modified >a &&
		rm -rf e &&
		rm b &&
		mkdir b &&
		echo b/f >b/f &&
		ln -s b e &&
		git submodule add ../submodule d &&
		git add -A &&
		git commit -m B2 &&

		git checkout --recurse-submodules B1
	)
'

test_expect_success SYMLINKS 'sequential checkout' '
	cp -R various various_sequential &&

	parallel_checkout_config 1 0 &&
	GIT_TRACE2="$(pwd)/trace" \
		git -C various_sequential checkout --recurse-submodules B2 &&
	test_workers_in_trace trace 0 &&

	verify_checkout various_sequential
'

for mode in parallel sequential-fallback
do
	case $mode in
	parallel)            workers=2 threshold=0   expected_workers=2 ;;
	sequential-fallback) workers=2 threshold=100 expected_workers=0 ;;
	esac

test_expect_success SYMLINKS "$mode checkout" '
	cp -R various various_$mode &&

	parallel_checkout_config $workers $threshold &&
	rm -f trace &&
	GIT_TRACE2="$(pwd)/trace" \
		git -C various_$mode checkout --recurse-submodules B2 &&
	test_workers_in_trace trace $expected_workers &&

	verify_checkout various_$mode
'

test_expect_success SYMLINKS "$mode checkout on clone" '
	parallel_checkout_config $workers $threshold &&
	rm -f trace &&
	GIT_TRACE2="$(pwd)/trace" git clone \
		--recurse-submodules --branch=B2 various various_${mode}_clone &&
	test_workers_in_trace trace $expected_workers &&

	verify_checkout various_${mode}_clone
'

done

# Just to be paranoid, actually compare the working trees' contents directly.
test_expect_success SYMLINKS 'compare the working trees' '
	rm -rf various_*/.git &&
	rm -rf various_*/d/.git &&

	diff -r various_sequential various_parallel &&
	diff -r various_sequential various_sequential-fallback &&
	diff -r various_sequential various_parallel_clone &&
	diff -r various_sequential various_sequential-fallback_clone
'

test_expect_success 'parallel checkout respects --[no]-force' '
	parallel_checkout_config 2 0 &&
	git init dirty &&
	(
		cd dirty &&
		mkdir D &&
		test_commit D/F &&
		test_commit F &&

		rm -rf D &&
		echo changed >D &&
		echo changed >F.t &&

		GIT_TRACE2="$(pwd)/trace" git checkout HEAD &&
		# There is nothing to do, so we expect 0 workers
		test_workers_in_trace trace 0 &&

		test_path_is_file D &&
		grep changed D &&
		grep changed F.t &&

		rm trace &&
		GIT_TRACE2="$(pwd)/trace" git checkout --force HEAD &&
		test_workers_in_trace trace 2 &&

		test_path_is_dir D &&
		grep D/F D/F.t &&
		grep F F.t
	)
'

test_expect_success SYMLINKS 'parallel checkout checks for symlinks in leading dirs' '
	parallel_checkout_config 2 0 &&
	git init symlinks &&
	(
		cd symlinks &&
		mkdir D untracked &&
		# Commit 2 files to have enough work for 2 parallel workers
		test_commit D/A &&
		test_commit D/B &&
		rm -rf D &&
		ln -s untracked D &&

		GIT_TRACE2="$(pwd)/trace" git checkout --force HEAD &&
		test_workers_in_trace trace 2 &&

		! test -h D &&
		grep D/A D/A.t &&
		grep D/B D/B.t
	)
'

test_done
