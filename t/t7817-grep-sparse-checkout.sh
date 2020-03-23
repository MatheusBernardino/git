#!/bin/sh

test_description='grep in sparse checkout

This test creates a repo with the following structure:

.
|-- a
|-- b
|-- dir
|   `-- c
`-- sub
    |-- A
    |   `-- a
    `-- B
	`-- b

Where . has non-cone mode sparsity patterns and sub is a submodule with cone
mode sparsity patterns. The resulting sparse-checkout should leave the following
structure:

.
|-- a
`-- sub
    `-- B
	`-- b
'

. ./test-lib.sh

test_expect_success 'setup' '
	echo "text" >a &&
	echo "text" >b &&
	mkdir dir &&
	echo "text" >dir/c &&

	git init sub &&
	(
		cd sub &&
		mkdir A B &&
		echo "text" >A/a &&
		echo "text" >B/b &&
		git add A B &&
		git commit -m sub &&
		git sparse-checkout init --cone &&
		git sparse-checkout set B
	) &&

	git submodule add ./sub &&
	git add a b dir &&
	git commit -m super &&
	git sparse-checkout init --no-cone &&
	git sparse-checkout set "/*" "!b" "!/*/" &&

	git tag -am t-commit t-commit HEAD &&
	tree=$(git rev-parse HEAD^{tree}) &&
	git tag -am t-tree t-tree $tree &&

	test_path_is_missing b &&
	test_path_is_missing dir &&
	test_path_is_missing sub/A &&
	test_path_is_file a &&
	test_path_is_file sub/B/b
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

test_expect_success 'grep <tree-ish> should ignore sparsity patterns' '
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

test_expect_success 'grep --recurse-submodules --cached should honor sparse checkout in submodule' '
	cat >expect <<-EOF &&
	a:text
	sub/B/b:text
	EOF
	git grep --recurse-submodules --cached "text" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --recurse-submodules <commit-ish> should honor sparse checkout in submodule' '
	commit=$(git rev-parse HEAD) &&
	cat >expect_commit <<-EOF &&
	$commit:a:text
	$commit:sub/B/b:text
	EOF
	cat >expect_t-commit <<-EOF &&
	t-commit:a:text
	t-commit:sub/B/b:text
	EOF
	git grep --recurse-submodules "text" $commit >actual_commit &&
	test_cmp expect_commit actual_commit &&
	git grep --recurse-submodules "text" t-commit >actual_t-commit &&
	test_cmp expect_t-commit actual_t-commit
'

test_done
