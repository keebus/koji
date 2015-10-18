/*
 * koji language - 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This is public domain software, read UNLICENSE for more information.
 */
 
#include "koji.h"
#include <string.h>
#include <stdio.h>

int main(int argc, char** argv)
{
	if (argc < 2 || strcmp(argv[1], "--help") == 0)
	{
		printf("usage: koji <file>\n");
		return 0;
	}

	koji_state* state = koji_create();

	if (koji_load_file(state, argv[1]) != KOJI_RESULT_OK)
	{
		return -1;
	}

	koji_run(state);

	koji_destroy(state);

	return 0;
}