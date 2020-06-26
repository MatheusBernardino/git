#!/bin/sh

test_description='PROVISORY'

. ./test-lib.sh

test_expect_success 'test oid_to_hex' '
	echo "3000000000000000000000000000000000000000" >expect &&
	test-tool oid-to-hex >actual &&
	test_cmp expect actual
'

test_done
