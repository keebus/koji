/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "koji.h"
#include <stdio.h>

int main(int argc, char** argv)
{
	if (argc < 2) {
		printf("usage: koji <filename>\n");
		return 0;
	}		

	koji_state_t *state = koji_open(0);

	if (koji_load_file(state, argv[1]))
		goto error;

	if (koji_run(state))
		goto error;

	goto cleanup;

error:
	printf("%s\n", koji_string(state, -1));
	koji_pop(state, 1);

cleanup:
	koji_close(state);
}