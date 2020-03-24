#!/bin/sh

test_description='grep in sparse checkout

This test creates the following dir structure:
.
| - a
| - b
| - dir
    | - c

Only "a" should be present due to the sparse checkout patterns:
"/*", "!/b" and "!/dir".
'

. ./test-lib.sh

test_expect_success 'setup' '
	echo "text" >a &&
	echo "text" >b &&
	mkdir dir &&
	echo "text" >dir/c &&
	git add a b dir &&
	git commit -m "initial commit" &&
	git tag -am t-commit t-commit HEAD &&
	tree=$(git rev-parse HEAD^{tree}) &&
	git tag -am t-tree t-tree $tree &&
	cat >.git/info/sparse-checkout <<-EOF &&
	/*
	!/b
	!/dir
	EOF
	git sparse-checkout init &&
	test_path_is_missing b &&
	test_path_is_missing dir &&
	test_path_is_file a
'

test_expect_success 'grep in working tree should honor sparse checkout' '
	cat >expect <<-EOF &&
	a:text
	EOF
	git grep "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --cached should honor sparse checkout' '
	cat >expect <<-EOF &&
	a:text
	EOF
	git grep --cached "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep <commit-ish> should honor sparse checkout' '
	commit=$(git rev-parse HEAD) &&
	cat >expect_commit <<-EOF &&
	$commit:a:text
	EOF
	cat >expect_t-commit <<-EOF &&
	t-commit:a:text
	EOF
	git grep "text" $commit >actual_commit &&
	test_cmp expect_commit actual_commit &&
	git grep "text" t-commit >actual_t-commit &&
	test_cmp expect_t-commit actual_t-commit
'

test_expect_success 'grep <tree-ish> should search outside sparse checkout' '
	commit=$(git rev-parse HEAD) &&
	tree=$(git rev-parse HEAD^{tree}) &&
	cat >expect_tree <<-EOF &&
	$tree:a:text
	$tree:b:text
	$tree:dir/c:text
	EOF
	cat >expect_t-tree <<-EOF &&
	t-tree:a:text
	t-tree:b:text
	t-tree:dir/c:text
	EOF
	git grep "text" $tree >actual_tree &&
	test_cmp expect_tree actual_tree &&
	git grep "text" t-tree >actual_t-tree &&
	test_cmp expect_t-tree actual_t-tree
'

for cmd in 'git grep --ignore-sparsity' 'git -c grep.ignoreSparsity grep' \
	   'git -c grep.ignoreSparsity=false grep --ignore-sparsity'
do
	test_expect_success "$cmd should search outside sparse checkout" '
		cat >expect <<-EOF &&
		a:text
		b:text
		dir/c:text
		EOF
		$cmd "text" >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd --cached should search outside sparse checkout" '
		cat >expect <<-EOF &&
		a:text
		b:text
		dir/c:text
		EOF
		$cmd --cached "text" >actual &&
		test_cmp expect actual
	'

	test_expect_success "$cmd <commit-ish> should search outside sparse checkout" '
		commit=$(git rev-parse HEAD) &&
		cat >expect_commit <<-EOF &&
		$commit:a:text
		$commit:b:text
		$commit:dir/c:text
		EOF
		cat >expect_t-commit <<-EOF &&
		t-commit:a:text
		t-commit:b:text
		t-commit:dir/c:text
		EOF
		$cmd "text" $commit >actual_commit &&
		test_cmp expect_commit actual_commit &&
		$cmd "text" t-commit >actual_t-commit &&
		test_cmp expect_t-commit actual_t-commit
	'
done

test_done
