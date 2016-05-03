/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
 #include "support.h"
 #include <malloc.h>
 
/*-----------------------------------------------------------------------------------------------*/
/* allocator                                                                                     */
/*-----------------------------------------------------------------------------------------------*/
void * default_malloc(void* userdata, koji_size_t size, koji_size_t alignment)
{
	(void)userdata;
	return _aligned_malloc(size, alignment); 
}

void * default_realloc(void* userdata, void *ptr, koji_size_t size, koji_size_t alignment)
{
	(void)userdata;
	return _aligned_realloc(ptr, size, alignment);
}

void default_free(void* userdata, void *ptr)
{
	(void)userdata;
	_aligned_free(ptr);
}

/*-----------------------------------------------------------------------------------------------*/
/* sequentially growable arrays                                                                  */
/*-----------------------------------------------------------------------------------------------*/
	static uint next_power2(uint v)
	{
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}

void * seqary_push_ex(void *pparray, uint *psize, allocator_t *alloc, uint elem_size,
                      uint elem_align, uint count)
{
	assert(count > 0);

	void **parray = (void **)pparray;
	const uint old_size = *psize;
	*psize += count;

	uint curr_capacity = old_size ? max_u(16, next_power2(old_size)) : 0;
	uint new_capacity = max_u(16, next_power2(*psize));
	
	if (curr_capacity < new_capacity) {
		*parray = kj_realloc(*parray, new_capacity * elem_size, elem_align, alloc);
	}

	return (char *)(*parray) + elem_size * old_size;
}
