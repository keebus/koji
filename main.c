/*
 * koji language - 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This is public domain software, read UNLICENSE for more information.
 */
 
#include "koji.h"
#include <string.h>
#include <stdio.h>

int debug_fn(koji_state* s, int nargs)
{
	for (int i = -nargs; i < 0; ++i)
	{
		if (i != -nargs) printf(" ");
		switch (koji_get_value_type(s, i))
		{
		case KOJI_TYPE_NIL: printf("nil"); break;
		case KOJI_TYPE_BOOL: printf(koji_to_int(s, i) ? "true" : "false"); break;
		case KOJI_TYPE_INT: printf("%lli", koji_to_int(s, i)); break;
		case KOJI_TYPE_REAL: printf("%f", koji_to_real(s, i)); break;
		case KOJI_TYPE_STRING: printf("%s", koji_get_string(s, i)); break;
		case KOJI_TYPE_TABLE: printf("table"); break;
		case KOJI_TYPE_CLOSURE: printf("closure"); break;
		default: printf("unknown\n");
		}
	}
	printf("\n");
	koji_pop(s, nargs);
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