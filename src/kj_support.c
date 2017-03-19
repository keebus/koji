/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_support.h"

 /*------------------------------------------------------------------------------------------------*/
 /* linear allocator                                                                               */
 /*------------------------------------------------------------------------------------------------*/
struct linear_allocator_page {
	char*                         cursor;
	int                           size;
	struct linear_allocator_page *next;
	char                          data[];
};

static char* align(char* ptr, int alignment)
{
	const uintptr_t alignment_minus_one = alignment - 1;
	return (char*)(((uintptr_t)ptr + alignment_minus_one) & ~alignment_minus_one);
}

kj_intern linear_allocator_t* linear_allocator_create(struct koji_allocator* allocator, int size)
{
	size = size < LINEAR_ALLOCATOR_PAGE_MIN_SIZE ? LINEAR_ALLOCATOR_PAGE_MIN_SIZE : size;
	linear_allocator_t* alloc = allocator->alloc(NULL, 0, sizeof(linear_allocator_t) + size, allocator->userdata);
	alloc->size = size;
	alloc->next = NULL;
	alloc->cursor = alloc->data;

	return alloc;
}

kj_intern void linear_allocator_destroy(linear_allocator_t *alloc, struct koji_allocator* allocator)
{
	while (alloc) {
		linear_allocator_t* temp = alloc->next;
		allocator->alloc(alloc, sizeof(linear_allocator_t) + alloc->size, 0, allocator->userdata);
		alloc = temp;
	}
}

kj_intern void linear_allocator_reset(linear_allocator_t **alloc, struct koji_allocator* allocator)
{
	linear_allocator_destroy((*alloc)->next, allocator);
	(*alloc)->next = NULL;
	(*alloc)->cursor = (*alloc)->data;
}

kj_intern void* linear_allocator_alloc(linear_allocator_t **alloc, struct koji_allocator* allocator, int size, int alignment)
{
	char* ptr = NULL;
	if (*alloc && (ptr = align((*alloc)->cursor, alignment)) >= (*alloc)->data + (*alloc)->size) {
		linear_allocator_t* temp = linear_allocator_create(allocator, size);
		temp->next = *alloc;
		*alloc = temp;
		ptr = align(temp->cursor, alignment);
	}
	(*alloc)->cursor = ptr + size;
	return ptr;
}


static int next_power2(int v)
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

kj_intern void* array_push_seq_ex(void* parray_, int* psize, struct koji_allocator* alloc, int elem_size, int count)
{
	assert(count > 0);

	void** parray = (void**)parray_;
	const int old_size = *psize;
	const int new_size = *psize + count;

	int curr_capacity = array_seq_capacity(*psize);
	int new_capacity = max_i(16, next_power2(new_size));

	if (curr_capacity < new_capacity) {
		*parray = alloc->alloc(*parray, 0, new_capacity * elem_size, alloc->userdata);
		if (!*parray) return NULL;
	}

	*psize = new_size;
	return (char *)(*parray) + elem_size * old_size;
}

kj_intern int array_seq_capacity(int size)
{
	return size ? max_i(16, next_power2(size)) : 0;
}

kj_intern bool array_reserve(void* parray_, int* psize, int* pcapacity, struct koji_allocator* alloc, int elem_size, int new_capacity)
{
	void** parray = (void**)parray_;

	/* Not enough capacity? */
	if (new_capacity > *pcapacity) {
		/* Calculate and set new capacity as the smallest large enough multiple of
		 * 10 */
		new_capacity = max_i(new_capacity, max_i(*pcapacity * 2, 10));

		/* Allocate new buffer and copy old values over */
		*parray = alloc->alloc(*parray, *pcapacity * elem_size, new_capacity * elem_size, alloc);
		if (!*parray) return false;

		*pcapacity = new_capacity;

		assert(*parray != NULL);
	}

	return true;
}

kj_intern bool array_resize(void* parray_, int* psize, int* pcapacity, struct koji_allocator* alloc, int elem_size, int new_size)
{
	void** parray = (void**)parray_;
	if (!array_reserve(parray_, psize, pcapacity, alloc, elem_size, new_size))
		return false;
	*psize = new_size;
	return true;
}

kj_intern void array_free(void* parray_, int* psize, int* pcapacity, struct koji_allocator* alloc)
{
	void** parray = (void**)parray_;
	alloc->alloc(*parray, *pcapacity, 0, alloc->userdata);
	*parray = NULL;
	*psize = 0;
	*pcapacity = 0;
}

kj_intern void *_array_push(void *parray_, int* psize, int* pcapacity, struct koji_allocator* alloc, int elem_size, int num_elements)
{
	void** parray = (void**)parray_;
	int prev_size = *psize;
	if (!array_resize(parray_, psize, pcapacity, alloc, elem_size, *psize + num_elements))
		return NULL;
	return ((char*)*parray) + prev_size * elem_size;
}

kj_intern uint64_t murmur2(const void * key, int len, uint64_t seed)
{
#ifdef KOJI_64 /* implementation for 64-bit cpus*/

	const uint64_t m = BIG_CONSTANT(0xc6a4a7935bd1e995);
	const int r = 47;

	uint64_t h = seed ^ (len * m);

	const uint64_t * data = (const uint64_t *)key;
	const uint64_t * end = data + (len / 8);

	while (data != end)
	{
		uint64_t k = *data++;

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}

	const unsigned char * data2 = (const unsigned char*)data;

	switch (len & 7)
	{
		case 7: h ^= (uint64_t)(data2[6]) << 48;
		case 6: h ^= (uint64_t)(data2[5]) << 40;
		case 5: h ^= (uint64_t)(data2[4]) << 32;
		case 4: h ^= (uint64_t)(data2[3]) << 24;
		case 3: h ^= (uint64_t)(data2[2]) << 16;
		case 2: h ^= (uint64_t)(data2[1]) << 8;
		case 1: h ^= (uint64_t)(data2[0]);
			h *= m;
	};

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;

#else /* implementation for 32-bit cpus */

	const uint32_t m = 0x5bd1e995;
	const int r = 24;

	uint32_t h1 = (uint32_t)(seed) ^ len;
	uint32_t h2 = (uint32_t)(seed >> 32);

	const uint32_t * data = (const uint32_t *)key;

	while (len >= 8)
	{
		uint32_t k1 = *data++;
		k1 *= m; k1 ^= k1 >> r; k1 *= m;
		h1 *= m; h1 ^= k1;
		len -= 4;

		uint32_t k2 = *data++;
		k2 *= m; k2 ^= k2 >> r; k2 *= m;
		h2 *= m; h2 ^= k2;
		len -= 4;
	}

	if (len >= 4)
	{
		uint32_t k1 = *data++;
		k1 *= m; k1 ^= k1 >> r; k1 *= m;
		h1 *= m; h1 ^= k1;
		len -= 4;
	}

	switch (len)
	{
		case 3: h2 ^= ((unsigned char*)data)[2] << 16;
		case 2: h2 ^= ((unsigned char*)data)[1] << 8;
		case 1: h2 ^= ((unsigned char*)data)[0];
			h2 *= m;
	};

	h1 ^= h2 >> 18; h1 *= m;
	h2 ^= h1 >> 22; h2 *= m;
	h1 ^= h2 >> 17; h1 *= m;
	h2 ^= h1 >> 19; h2 *= m;

	uint64_t h = h1;

	h = (h << 32) | h2;

	return h;
#endif
}