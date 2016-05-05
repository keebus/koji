/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
#include "kj_api.h"
#include "kj_support.h"

struct koji_state {
   allocator_t allocator;
};

KOJI_API
koji_state * koji_open(koji_malloc_fn malloc_fn, koji_realloc_fn realloc_fn, koji_free_fn free_fn,
                       void *alloc_userdata)
{
   /* setup the allocator object */
   allocator_t allocator = {
      .userdata = alloc_userdata,
      .malloc = malloc_fn ? malloc_fn : default_malloc,
      .realloc = realloc_fn ? realloc_fn : default_realloc,
      .free = free_fn ? free_fn : default_free,
   };

   koji_state *state = kj_alloc(koji_state, 1, &allocator);
   state->allocator = allocator;

   return state;
}

KOJI_API void koji_close(koji_state *state)
{
   kj_free(state, &state->allocator);
}
