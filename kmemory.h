/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kplatform.h"

/*
 * Allocates an array of `count` elements of specified `type` using allocator
 *  pointed by `allocp`.
 */
#define kalloc(type, count, allocp)                                            \
	((type *)((allocp)->allocate(sizeof(type) * count, (allocp)->user)))

/*
 * Reallocates typed `ptr` from `oldcount` to `newcount` using specified
 *  allocator pointed by `allocp`.
 */
#define krealloc(ptr, oldcount, newcount, allocp)                              \
	((allocp)->reallocate(ptr, sizeof(*ptr) * oldcount,                         \
	                      sizeof(*ptr) * newcount, (allocp)->user))

/*
 * Deallocates memory allocation pointed by typed `ptr`, where `count` is the
 *  number of elements in the allocation and `allocp` is a pointer to the
 *  allocator to use.
 */
#define kfree(ptr, count, allocp)                                              \
	((allocp)->deallocate(ptr, (sizeof *ptr) * count, (allocp)->user))

#ifndef KOJI_NO_DEFAULT_ALLOCATOR
/*
 * Global instance of a default allocator based on malloc.
 */
extern struct koji_allocator s_default_allocator;
#endif

/* Dynamic array macros */

/*
 * Pushes an element to the array `arrayp` with `sizep` elements and `lenp`
 * capacity. The macro expands to a "reference" to the new element in the array
 * (i.e. you can assign to it).
 */
#define array_push(arrayp, sizep, lenp, allocp)                                \
	((*arrayp)[array_push_ex(arrayp, sizeof **arrayp, sizep, lenp, 1, allocp)])

/*
 * Pushes an element to a an array `arrayp` with `sizep` elements assuming that
 * the current capacity is the smallest power of two integer large enough to
 * contain `sizep`. The macro expands to a "reference" to the new element in the
 * array (i.e. you can assign to it).
 */
#define array_seqpush(arrayp, sizep, allocp)                                   \
	((*arrayp)[array_seqpush_ex(arrayp, sizeof **arrayp, sizep, 1, allocp)])

/*
 * Frees the array `arrayp` with capacity `len`.
 */
#define array_free(arrayp, len, allocp)                                        \
	(allocp)->deallocate(arrayp, sizeof(*arrayp) * len, (allocp)->user)

/*
 * Frees the array `arrayp` with capacity the minimum power of two integer
 * larger than `size`.
 */
#define array_seqfree(arrayp, size, allocp)                                          \
	(allocp)->deallocate(arrayp, sizeof *arrayp * array_minlen(size), (allocp)->user)

/*
 * Returns the minimum power of two integer large enough to contain size.
 */
kintern uint32_t
array_minlen(uint32_t size);

/*
 * Pushes `pushcnt` element of size `elemsize` into the array `arraypp` with
 * `sizep` number of elements and `lenp` capacity.
 */
kintern uint32_t
array_push_ex(void **arraypp, uint32_t elemsize, uint32_t *sizep,
              uint32_t *lenp, uint32_t pushcnt, struct koji_allocator *);

/*
 * Pushes `pushcnt` element of size `elemsize` into the array `arraypp` with
 * `sizep` number of elements and capacity being the smallest power of two
 * integer large enough to contain `sizep`.
 */
kintern uint32_t
array_seqpush_ex(void **arraypp, uint32_t elemsize, uint32_t *sizep,
                 uint32_t pushcnt, struct koji_allocator *);

/*
 * Scratch Buffer
 * A scratch buffer provides a mean for fast allocation of temporary memory. It
 * works by automatically allocating and managing pages of memory, linked
 * together. To fulfil an allocation request, the scratch buffer first sees
 * whether the current page has any space left to fit the allocation, otherwise
 * it allocates a new page large enough to fit the allocation and inserts it in
 * the pages list for tracking. A scratch buffer cursor can be reset to any
 * page/offset without deallocating existing pages.
 */

struct scratch_page {
	/* pointer to the next page */
	struct scratch_page *next;
	/* end of the buffer */
	char *end;
	/* begin of the buffer */
	char buffer[];
};

/*
 * Marks a byte position [cur] marking the next free byte within [page] scratch
 * page. Used to record the current state of the compiler scratch allocator
 * to be restored later when latest temporary allocations are no longer needed.
 */
struct scratch_pos {
	/* current page with free space */
	struct scratch_page *page;
	/* first free byte within the page */
	char *curr;
};

/*
 *
 */
kintern struct scratch_page *
scratch_create(struct scratch_pos *, struct koji_allocator *);

/*
 *
 */
kintern void
scratch_delete(struct scratch_page *head, struct koji_allocator *);

/*
 *
 */
kintern void *
scratch_alloc_ex(struct scratch_pos *, int32_t size, int32_t alignment,
                 struct koji_allocator *);

/*
 *
 */
#define scratch_alloc(posp, type, allocp)                                      \
	((type *)scratch_alloc_ex(posp, sizeof(type), kalignof(type), allocp))
