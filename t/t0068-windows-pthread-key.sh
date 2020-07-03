#!/bin/sh

test_description='pthread_key emulation for Windows'

. ./test-lib.sh

test_expect_success MINGW,PTHREADS 'can create and delete key' '
	test-tool windows-pthread-key setup-only
'

test_expect_success MINGW,PTHREADS 'pthread_getspecific return NULL when unset' '
	test-tool windows-pthread-key first-get
'

test_expect_success MINGW,PTHREADS 'can retrieve a value set on TLS' '
	test-tool windows-pthread-key set-and-get
'

test_expect_success MINGW,PTHREADS 'destructor callback is called on thread exit' '
	test-tool windows-pthread-key destructor
'

test_expect_success MINGW,PTHREADS 'destructor callback is not called for NULL values' '
	test-tool windows-pthread-key destructor-on-null
'

test_expect_success MINGW,PTHREADS 'destructor callback is not called when the key is deleted' '
	test-tool windows-pthread-key delete
'

test_done
