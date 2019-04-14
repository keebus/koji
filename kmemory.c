/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kmemory.h"

#ifdef _WIN32
#include <intrin.h> /* for _BitScanReverse */
#endif

static uintptr_t
alignu(uintptr_t val, uint32_t align)
{
	const uintptr_t align_minus_one = align - 1;
	return ((uintptr_t)val + align_minus_one) & ~align_minus_one;
}

static void *
alignp(void *val, uint32_t align)
{
	return (void *)alignu((uintptr_t)val, align);
}

#ifndef KOJI_NO_DEFAULT_ALLOCATOR

void *
default_allocate(uint32_t size, void *user)
{
	return malloc(size);
}

void *
default_reallocate(void *ptr, uint32_t oldsize, uint32_t newsize, void *user)
{
	return realloc(ptr, newsize);
}

void
default_deallocate(void *ptr, uint32_t totsize, void *user)
{
	free(ptr);
}

kintern struct koji_allocator s_default_allocator = {
    NULL, default_allocate, default_reallocate, default_deallocate};

#endif

/* Buffer */

kintern uint32_t
array_minlen(uint32_t size)
{
	unsigned long index;
	if (size == 0)
		return 0;
	_BitScanReverse(&index, (size - 1) | 0x3f);
	return (uint32_t)(1U << (index + 1));
}

kintern int32_t
array_push_ex(void **arraypp, uint32_t elemsize, uint32_t *sizep,
              uint32_t *lenp, uint32_t pushcnt, struct koji_allocator *allocp)
{
	uint32_t const oldsize = *sizep;
	*sizep = oldsize + pushcnt;
	if (*sizep > *lenp) {
		uint32_t const newlen = array_minlen(*sizep);
		*arraypp = allocp->reallocate(*arraypp, *lenp * elemsize,
		                              newlen * elemsize, allocp->user);
		*lenp = newlen;
	}
	return oldsize;
}

kintern uint32_t
array_seqpush_ex(void **arraypp, uint32_t elemsize, uint32_t *sizep,
                 uint32_t pushcnt, struct koji_allocator *allocp)
{
	uint32_t len = array_minlen(*sizep);
	return array_push_ex(arraypp, elemsize, sizep, &len, pushcnt, allocp);
}

/* Scratch buffer */

static int32_t
scratch_size(struct scratch_page *p)
{
	return (int32_t)(p->end - p->buffer);
}

static struct scratch_page *
scratch_page_new(struct scratch_page *last, int32_t size,
                 struct koji_allocator *alloc)
{
	int32_t pagesz = sizeof(struct scratch_page) + size;
	struct scratch_page *page = alloc->allocate(pagesz, alloc->user);
	page->next = last->next;
	page->end = (char *)page + pagesz;
	last->next = page;
	return page;
}

kintern struct scratch_page *
scratch_create(struct scratch_pos *pos, struct koji_allocator *alloc)
{
	struct scratch_page *dummy_next = 0;
	pos->page = scratch_page_new((struct scratch_page *)&dummy_next, 1, alloc);
	pos->curr = pos->page->buffer;
	return pos->page;
}

kintern void
scratch_delete(struct scratch_page *head, struct koji_allocator *alloc)
{
	struct scratch_page *page = head;
	while (page) {
		struct scratch_page *next = page->next;
		alloc->deallocate(page, scratch_size(page), alloc->user);
		page = next;
	}
}

kintern void *
scratch_alloc_ex(struct scratch_pos *pos, int32_t size, int32_t alignment,
                 struct koji_allocator *alloc)
{
	char *ptr = alignp(pos->curr, alignment);
	if (ptr + size > pos->page->end) {
		/* does next page has enough space to hold allocation? */
		if (pos->page->next && (scratch_size(pos->page->next) >= size)) {
			pos->page = pos->page->next;
		} else {
			/* no next page after current page in chain, or next page does not hold
			   enough space for allocation; push new page between the two */
			pos->page = scratch_page_new(pos->page, size, alloc);
		}
		pos->curr = pos->page->buffer;
		return scratch_alloc_ex(pos, size, alignment, alloc);
	}
	pos->curr = ptr + size;
	return ptr;
}
