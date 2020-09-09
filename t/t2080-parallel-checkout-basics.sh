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
		git init d &&
		test_commit -C d f &&
		git submodule add ./d &&
		git add -A &&
		git commit -m B2 &&

		git checkout --recurse-submodules B1
	)
'

test_expect_success SYMLINKS 'sequential checkout' '
	cp -R various various_sequential &&
	set_checkout_config 1 0 &&
	test_checkout_workers 0 \
		git -C various_sequential checkout --recurse-submodules B2 &&
	verify_checkout various_sequential
'

test_expect_success SYMLINKS 'parallel checkout' '
	cp -R various various_parallel &&
	set_checkout_config 2 0 &&
	test_checkout_workers 2 \
		git -C various_parallel checkout --recurse-submodules B2 &&
	verify_checkout various_parallel
'

test_expect_success SYMLINKS 'fallback to sequential checkout (threshold)' '
	cp -R various various_sequential_fallback &&
	set_checkout_config 2 100 &&
	test_checkout_workers 0 \
		git -C various_sequential_fallback checkout --recurse-submodules B2 &&
	verify_checkout various_sequential_fallback
'

test_expect_success SYMLINKS 'parallel checkout on clone' '
	git -C various checkout --recurse-submodules B2 &&
	set_checkout_config 2 0 &&
	test_checkout_workers 2 \
		git clone --recurse-submodules various various_parallel_clone &&
	verify_checkout various_parallel_clone
'

test_expect_success SYMLINKS 'fallback to sequential checkout on clone (threshold)' '
	git -C various checkout --recurse-submodules B2 &&
	set_checkout_config 2 100 &&
	test_checkout_workers 0 \
		git clone --recurse-submodules various various_sequential_fallback_clone &&
	verify_checkout various_sequential_fallback_clone
'

# Just to be paranoid, actually compare the working trees' contents directly.
test_expect_success SYMLINKS 'compare the working trees' '
	rm -rf various_*/.git &&
	rm -rf various_*/d/.git &&

	diff -r various_sequential various_parallel &&
	diff -r various_sequential various_sequential_fallback &&
	diff -r various_sequential various_parallel_clone &&
	diff -r various_sequential various_sequential_fallback_clone
'

test_expect_success 'parallel checkout respects --[no]-force' '
	set_checkout_config 2 0 &&
	git init dirty &&
	(
		cd dirty &&
		mkdir D &&
		test_commit D/F &&
		test_commit F &&

		rm -rf D &&
		echo changed >D &&
		echo changed >F.t &&

		# We expect 0 workers because there is nothing to be done
		test_checkout_workers 0 git checkout HEAD &&
		test_path_is_file D &&
		grep changed D &&
		grep changed F.t &&

		test_checkout_workers 2 git checkout --force HEAD &&
		test_path_is_dir D &&
		grep D/F D/F.t &&
		grep F F.t
	)
'

test_expect_success SYMLINKS 'parallel checkout checks for symlinks in leading dirs' '
	set_checkout_config 2 0 &&
	git init symlinks &&
	(
		cd symlinks &&
		mkdir D untracked &&
		# Commit 2 files to have enough work for 2 parallel workers
		test_commit D/A &&
		test_commit D/B &&
		rm -rf D &&
		ln -s untracked D &&

		test_checkout_workers 2 git checkout --force HEAD &&
		! test -h D &&
		grep D/A D/A.t &&
		grep D/B D/B.t
	)
'

test_done
