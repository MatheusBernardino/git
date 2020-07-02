#!/bin/sh

test_description='pthread_key emulation for Windows'

. ./test-lib.sh

test_expect_success MINGW,PTHREADS 'pthread_getspecific returns NULL when no value is set' '
	echo "(null)" >expect &&
	test-tool windows-pthread-key --no-set >actual &&
	test_cmp expect actual
'

test_expect_success MINGW,PTHREADS 'pthread_getspecific can retrieve a set value' '
	echo VALUE >expect &&
	test-tool windows-pthread-key VALUE >actual &&
	test_cmp expect actual
'

test_expect_success MINGW,PTHREADS 'destructor callback is called on thread exit' '
	cat >expect <<-EOF &&
	VALUE
	destructor received "VALUE"
	EOF
	test-tool windows-pthread-key --destructor VALUE >actual &&
	test_cmp expect actual
'

test_expect_success MINGW,PTHREADS 'destructor callback is not called for not set values' '
	echo "(null)" >expect &&
	test-tool windows-pthread-key --destructor --no-set >actual &&
	test_cmp expect actual
'

test_expect_success MINGW,PTHREADS 'destructor callback is not called for values explicitly set to NULL' '
	echo "(null)" >expect &&
	test-tool windows-pthread-key --destructor --set-to-null >actual &&
	test_cmp expect actual
'

test_expect_success MINGW,PTHREADS 'destructor callback is not called after the key is deleted' '
	echo VALUE >expect &&
	test-tool windows-pthread-key --destructor --delete-key-in-thread VALUE >actual &&
	test_cmp expect actual
'

test_done
