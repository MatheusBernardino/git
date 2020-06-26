#include "test-tool.h"
#include "cache.h"
#include "thread-utils.h"

int cmd__oid_to_hex(int argc, const char **argv)
{
	struct object_id oid = {.hash = "0"};

#if !HAVE_THREADS
	die("NO HAVE_THREADS!!!");
#endif
	setup_git_directory();
	
	printf("%s\n", oid_to_hex(&oid));

	return 0;
}
