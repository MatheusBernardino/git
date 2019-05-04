#!/bin/sh
#
# Build and test Git
#

. ${0%/*}/lib.sh

case "$CI_OS_NAME" in
windows*) cmd //c mklink //j t\\.prove "$(cygpath -aw "$cache_dir/.prove")";;
*) ln -s "$cache_dir/.prove" t/.prove;;
esac

CORES=2

echo "Compiling and running tests with $CORES jobs"

make -j$CORES
make -j$CORES test
if test "$jobname" = "linux-gcc"
then
	export GIT_TEST_SPLIT_INDEX=yes
	export GIT_TEST_FULL_IN_PACK_ARRAY=true
	export GIT_TEST_OE_SIZE=10
	export GIT_TEST_OE_DELTA_SIZE=5
	export GIT_TEST_COMMIT_GRAPH=1
	export GIT_TEST_MULTI_PACK_INDEX=1
	make -j$CORES test
fi

check_unignored_build_artifacts

save_good_tree
