#!/bin/sh

test_description='git add in sparse checked out working trees'

. ./test-lib.sh

SPARSE_ENTRY_BLOB=""

# Optionally take a printf format string to write to the sparse_entry file
setup_sparse_entry () {
	rm -f sparse_entry &&
	git update-index --force-remove sparse_entry &&

	if test $# -eq 1
	then
		printf "$1" >sparse_entry
	else
		>sparse_entry
	fi &&
	git add sparse_entry &&
	git update-index --skip-worktree sparse_entry &&
	SPARSE_ENTRY_BLOB=$(git rev-parse :sparse_entry)
}

test_sparse_entry_unchanged () {
	echo "100644 $SPARSE_ENTRY_BLOB 0	sparse_entry" >expected &&
	git ls-files --stage sparse_entry >actual &&
	test_cmp expected actual
}

test_expect_success 'git add does not remove sparse entries' '
	setup_sparse_entry &&
	rm sparse_entry &&
	git add sparse_entry &&
	test_sparse_entry_unchanged
'

for opt in -A .
do
	test_expect_success "git add $opt does not remove sparse entries" '
		setup_sparse_entry &&
		rm sparse_entry &&
		test_when_finished rm -f .gitignore &&
		cat >.gitignore <<-EOF &&
		*
		!/sparse_entry
		EOF
		git add $opt &&
		test_sparse_entry_unchanged
	'
done

for opt in "" -f -u --ignore-removal --dry-run
do
	test_expect_success "git add${opt:+ $opt} does not update sparse entries" '
		setup_sparse_entry &&
		echo modified >sparse_entry &&
		git add $opt sparse_entry &&
		test_sparse_entry_unchanged
	'
done

test_expect_success 'git add --refresh does not update sparse entries' '
	setup_sparse_entry &&
	git ls-files --debug sparse_entry | grep mtime >before &&
	test-tool chmtime -60 sparse_entry &&
	git add --refresh sparse_entry &&
	git ls-files --debug sparse_entry | grep mtime >after &&
	test_cmp before after
'

test_expect_failure 'git add --chmod does not update sparse entries' '
	setup_sparse_entry &&
	git add --chmod=+x sparse_entry &&
	test_sparse_entry_unchanged &&
	! test -x sparse_entry
'

test_expect_failure 'git add --renormalize does not update sparse entries' '
	test_config core.autocrlf false &&
	setup_sparse_entry "LINEONE\r\nLINETWO\r\n" &&
	echo "sparse_entry text=auto" >.gitattributes &&
	git add --renormalize sparse_entry &&
	test_sparse_entry_unchanged
'

test_done
