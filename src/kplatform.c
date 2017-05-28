/*
 * koji scripting language
 * 
 * Copyright (C) 2017 Canio Massimo Tristano
 * 
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kplatform.h"

/* default allocator */

#ifndef KOJI_NO_DEFAULT_ALLOC

#include <malloc.h>

static void *
default_alloc_alloc(int32_t size, void *user)
{
   (void)user;
   return malloc(size);
}

static void *
default_alloc_realloc(void *ptr, int32_t oldsize, int32_t newsize, void *user)
{
   (void)oldsize;
   (void)user;
   return realloc(ptr, newsize);
}

static void
default_alloc_free(void *ptr, int32_t size, void *user)
{
   (void)user;
   (void)size;
   free(ptr);
}

kintern struct koji_allocator *
default_alloc(void)
{
   static struct koji_allocator defalloc = {
      NULL,
      default_alloc_alloc,
      default_alloc_realloc,
      default_alloc_free
   };
   return &defalloc;
}

#else

kintern struct koji_allocator *
default_alloc(void)
{
   return NULL;
}

#endif


 /* linear alloc */
struct linear_alloc_page {
	char *cursor;
	int32_t size;
	struct linear_alloc_page *next;
	char data;
};

static char *
align(char *ptr, int32_t align)
{
	const uintptr_t align_minus_one = align - 1;
	return (char*)(((uintptr_t)ptr + align_minus_one) & ~align_minus_one);
}

kintern linear_alloc_t *
linear_alloc_create(struct koji_allocator *alloc, int32_t size)
{
	size = max_i32(size, LINEAR_ALLOC_PAGE_MIN_SIZE);

	linear_alloc_t *lalloc =
      alloc->alloc(sizeof(linear_alloc_t) + size - 1 /* data */, alloc->user);

   lalloc->cursor = &lalloc->data;
	lalloc->size = size;
	lalloc->next = NULL;

	return lalloc;
}

kintern void
linear_alloc_destroy(linear_alloc_t *lalloc, struct koji_allocator *alloc)
{
	while (lalloc) {
		linear_alloc_t *temp = lalloc->next;
		alloc->free(lalloc, sizeof(linear_alloc_t) + lalloc->size, alloc->user);
		lalloc = temp;
	}
}

kintern void
linear_alloc_reset(linear_alloc_t **lalloc, struct koji_allocator *alloc)
{
	linear_alloc_destroy((*lalloc)->next, alloc);
	(*lalloc)->next = NULL;
	(*lalloc)->cursor = &(*lalloc)->data;
}

kintern void *
linear_alloc_alloc(linear_alloc_t **lalloc, struct koji_allocator *alloc,
   int32_t size, int32_t alignm)
{
	char *ptr = align((*lalloc)->cursor, alignm);
	if (ptr >= &(*lalloc)->data + (*lalloc)->size) {
		linear_alloc_t *temp = linear_alloc_create(alloc, size);
		temp->next = *lalloc;
		*lalloc = temp;
		ptr = align(temp->cursor, alignm);
	}
	(*lalloc)->cursor = ptr + size;
	return ptr;
}


/* array */

kintern void *
array_seq_new(struct koji_allocator *alloc, int32_t elemsize)
{
   return alloc->alloc(array_seq_len(0) * elemsize, alloc->user);
}

kintern void *
array_seq_push_ex(void *arrayp_, int32_t *size, struct koji_allocator *alloc,
   int32_t elemsize, int32_t count)
{
	assert(count > 0);

	void **arrayp = (void**)arrayp_;
	const int32_t oldsize = *size;
	const int32_t newsize = *size + count;

	int32_t currlen = array_seq_len(*size);
	int32_t newlen =  array_seq_len(newsize);

	if (currlen < newlen) {
		*arrayp = alloc->alloc(newlen * elemsize, alloc->user);
		if (!*arrayp) return NULL;
	}

	*size = newsize;
	return (char *)(*arrayp) + elemsize * oldsize;
}

kintern void
array_seq_free(void *arrayp, int32_t *size, struct koji_allocator *alloc,
   int32_t elemsize)
{
   alloc->free(*(void**)arrayp, array_seq_len(*size * elemsize),
      alloc->user);
   *(void **)arrayp = NULL;
   *size = 0;
}

kintern int32_t
array_seq_len(int32_t size)
{
   size--;
   size |= size >> 1;
   size |= size >> 2;
   size |= size >> 4;
   size |= size >> 8;
   size |= size >> 16;
   size++;
   return max_i32(16, size);
}

kintern bool
array_reserve(void *arrayp, int32_t *size, int32_t *len, struct koji_allocator *alloc,
   int32_t elemsize, int32_t newlen)
{
	void **array = (void**)arrayp;

	/* Not enough capacity? */
	if (newlen > *len) {
		/* Calculate and set new capacity as the smallest large enough multiple
		   of 10 */
		newlen = max_i32(newlen, max_i32(*len * 2, 10));

		/* Allocate new buffer and copy old values over */
		*array = alloc->realloc(*array, *len * elemsize, newlen * elemsize,
                              alloc);
		if (!*array) return false;

		*len = newlen;

		assert(*array != NULL);
	}

	return true;
}

kintern bool
array_resize(void *arrayp, int32_t *size, int32_t *len,
   struct koji_allocator *alloc, int32_t elemsize, int32_t newsize)
{
	if (!array_reserve(arrayp, size, len, alloc, elemsize, newsize))
		return false;
	*size = newsize;
	return true;
}

kintern void
array_free(void *arrayp, int32_t *size, int32_t *len,
   struct koji_allocator *alloc, int32_t elemsize)
{
	void **array = (void**)arrayp;
	alloc->free (*array, *len * elemsize, alloc->user);
	*array = NULL;
	*size = 0;
	*len = 0;
}

kintern void *
_array_push(void *arrayp, int32_t *size, int32_t *len,
   struct koji_allocator *alloc, int32_t elemsize, int32_t count)
{
	void **array = (void**)arrayp;
	int32_t prev_size = *size;
	if (!array_resize(arrayp, size, len, alloc, elemsize, *size + count))
		return NULL;
	return ((char*)*array) + prev_size * elemsize;
}

kintern uint64_t
murmur2(const void *key, int32_t len, uint64_t seed)
{
#ifdef KOJI_64 /* implementation for 64-bit cpus*/

	const uint64_t m = 0xc6a4a7935bd1e995ULL;
	const int32_t r = 47;

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
	const int32_t r = 24;

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
