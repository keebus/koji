/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
#include "api.h"
#include "support.h"

struct koji_state {
	int data;
};

KOJI_API
koji_state * koji_open(koji_malloc_fn malloc_fn, koji_realloc_fn realloc_fn, koji_free_fn free_fn,
                       void *alloc_userdata)
{
	
	return 0;
}

KOJI_API
void koji_close(koji_state *state)
{
	
}
