#!/bin/sh

test_description='git add in sparse checked out working trees'

. ./test-lib.sh

SPARSE_ENTRY_BLOB=""

# Optionally take a string for the entry's contents
setup_sparse_entry()
{
	if test -f sparse_entry
	then
		rm sparse_entry
	fi &&
	git update-index --force-remove sparse_entry &&

	if test "$#" -eq 1
	then
		printf "$1" >sparse_entry
	else
		printf "" >sparse_entry
	fi &&
	git add sparse_entry &&
	git update-index --skip-worktree sparse_entry &&
	SPARSE_ENTRY_BLOB=$(git rev-parse :sparse_entry)
}

test_sparse_entry_unchanged() {
	echo "100644 $SPARSE_ENTRY_BLOB 0	sparse_entry" >expected &&
	git ls-files --stage sparse_entry >actual &&
	test_cmp expected actual
}

test_expect_success "git add does not remove SKIP_WORKTREE entries" '
	setup_sparse_entry &&
	rm sparse_entry &&
	git add sparse_entry &&
	test_sparse_entry_unchanged
'

test_expect_success "git add -A does not remove SKIP_WORKTREE entries" '
	setup_sparse_entry &&
	rm sparse_entry &&
	git add -A &&
	test_sparse_entry_unchanged
'

for opt in "" -f -u --ignore-removal
do
	if test -n "$opt"
	then
		opt=" $opt"
	fi

	test_expect_success "git add$opt does not update SKIP_WORKTREE entries" '
		setup_sparse_entry &&
		echo modified >sparse_entry &&
		git add $opt sparse_entry &&
		test_sparse_entry_unchanged
	'
done

test_expect_success 'git add --refresh does not update SKIP_WORKTREE entries' '
	setup_sparse_entry &&
	test-tool chmtime -60 sparse_entry &&
	git add --refresh sparse_entry &&

	# We must unset the SKIP_WORKTREE bit, otherwise
	# git diff-files would skip examining the file
	git update-index --no-skip-worktree sparse_entry &&

	echo sparse_entry >expected &&
	git diff-files --name-only sparse_entry >actual &&
	test_cmp actual expected
'

test_expect_success 'git add --chmod does not update SKIP_WORKTREE entries' '
	setup_sparse_entry &&
	git add --chmod=+x sparse_entry &&
	test_sparse_entry_unchanged
'

test_expect_success 'git add --renormalize does not update SKIP_WORKTREE entries' '
	test_config core.autocrlf false &&
	setup_sparse_entry "LINEONE\r\nLINETWO\r\n" &&
	echo "sparse_entry text=auto" >.gitattributes &&
	git add --renormalize sparse_entry &&
	test_sparse_entry_unchanged
'

test_done
