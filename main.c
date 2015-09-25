// koji language - 2015 Canio Massimo Tristano
// See LICENSE.txt for license and copyright information.

#include <koji.h>
#include <string.h>
#include <stdio.h>

int debug_fn(koji_state* s, int nargs)
{
	printf("debug: %d\n", (int)koji_to_int(s, -1));
	koji_pop(s, 1);
	return 0;
}

int main(int argc, char** argv)
{
	if (argc < 2 || strcmp(argv[1], "--help") == 0)
	{
		printf("usage: koji <file>\n");
		return 0;
	}

	koji_state* state = koji_create();

	koji_static_function(state, "debug", debug_fn, 1);

	if (koji_load_file(state, argv[1]) != KOJI_RESULT_OK)
	{
		return -1;
	}

	koji_run(state);

	koji_destroy(state);

	return 0;
}