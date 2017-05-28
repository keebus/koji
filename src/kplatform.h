/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "koji.h"

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>

/* add compiler specific warning ignores */
#if defined(__GNUC__) || defined(__clang__)
   #pragma GCC diagnostic ignored "-Wmultichar"
   /* #pragma GCC diagnostic ignored "-Wmissing-field-initializers" */
#endif

#ifdef KOJI_AMALGAMATE
#define kintern static
#else
#define kintern
#endif

#ifdef _WIN64
#define KOJI_64
#endif

/*
 * Platform independent language extensions.
 */

#define kalignof(T) __alignof(T)
#define kalloca _alloca

/*
 * Convenience macros that unwrap to koji alloc malloc/realloc/free call.
 */

/*
 * Allocates an array of [count] elements of specified [type]
 */
#define kalloc(type, count, allocp)\
   ((allocp)->alloc(sizeof(type) * count, (allocp)->user))

/*
 * Reallocates [ptr] from [oldcount] to [newcount] using specified [allocp] */
#define krealloc(ptr, oldcount, newcount, allocp)\
   ((allocp)->realloc(ptr, sizeof(*ptr) * oldcount,\
                    sizeof(*ptr) * newcount, (allocp)->user))

#define kfree(ptr, count, allocp)\
	((allocp)->free(ptr, (sizeof *ptr) * count, (allocp)->user))

static int32_t min_i32(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t max_i32(int32_t a, int32_t b) { return a > b ? a : b; }

/*
 * If enabled, it returns the default allocator using malloc, otherwise NULL.
 * Disable by defining KOJI_NO_DEFAULT_ALLOC
 */
kintern struct koji_allocator *
default_alloc(void);

/*
 * A linear alloc provides a way to allocate memory linearly (thus quickly) and
 * then clearing it up later on when all allocations are not needed. Use it to
 * freely allocate scratch memory that is soon going to be freed altogether.
 */

/*
 * The minimum size of a linear alloc page in bytes.
 */
#define LINEAR_ALLOC_PAGE_MIN_SIZE 1024

/*
 * A linear alloc structure. Clients are not required to use any
 */
typedef struct linear_alloc_page linear_alloc_t;

/*
 * Creates a new linear alloc of specified size (which will be maxed with
 * LINEAR_ALLOC_PAGE_MIN_SIZE). Use [alloc] for page allocations.
 */
kintern linear_alloc_t *
linear_alloc_create(struct koji_allocator *alloc, int32_t size);

/*
 * Destroys the linear alloc [talloc] which was created with specified [alloc]
 */
kintern void
linear_alloc_destroy(linear_alloc_t *lalloc, struct koji_allocator *alloc);

/*
 * Resets all current pages of specified linear alloc [talloc]. All pages after
 * the first one are deallocated.
 */
kintern void
linear_alloc_reset(linear_alloc_t **talloc, struct koji_allocator *alloc);

/*
 * Allocates a chunk of memory of given [size] and [alignment] from specified
 * linear alloc [talloc] using [alloc] if a new page needs to be allocate.
 */
kintern void*
linear_alloc_alloc(linear_alloc_t **talloc, struct koji_allocator *alloc,
   int32_t size, int32_t align);


/*
 * A sequentially growing array is a simple array that is allowed to grow
 * dynamically one or more elements at a time. Initialize your array with
 * something like 'int32_t *my_ints = NULL' and 'uint num_ints = 0'. Then call
 * seqarray_push() to increase the size of the array by one. This function does
 * not actually reallocate the array every time but allocates to the smallest
 * power of two elements large enough to contain the current num of
 * elements. The minimum num of elements is 16. Therefore when there are 16
 * elements and the 17th is being pushed, the array is reallocated to have a
 * capacity of 32 elements.
 */

/*
 * Grows the sequential (pointed) array [parray] of current (pointed) [sizep]
 * by [n] elements of specified [type] using specified [allocp] pointer to
 * alloc.
 */
#define array_seq_push(arraypp, sizep, allocp, type, n)\
   ((type *)array_seq_push_ex(arraypp, sizep, allocp, sizeof(type), n))

/*
 * Pushes [n] elements of specified [type] in array pointed by [parray] defined
 * using [array_type()]. Returns a typed pointer to the first element pushed.
 */
#define array_push(arraypp, sizep, lenp, allocp, type, n)\
	((type *)_array_push(arraypp, sizep, lenp, allocp, sizeof(type), n))

/*
 * Allocates a new sequential array with length 16.
 * Only use array_seq_push on returned array.
 */
kintern void *
array_seq_new(struct koji_allocator *alloc, int32_t elemsize);

/*
 * Low-level function to grow a sequentially growable array. Used by higher
 * level functions [seqary_push] and [seqary_push_n], use those.
 */
kintern void *
array_seq_push_ex(void *arrayp, int32_t *size, struct koji_allocator *alloc,
   int32_t elemsize, int32_t count);

/*
 * Frees sequential array arrayp with specified [size] num of elements
 * and [elemsize] element size.
 */
kintern void
array_seq_free(void *arrayp, int32_t *size, struct koji_allocator *alloc,
   int32_t elemsize);

/*
 * Returns the capacity (least greater power of two) of sequentially pushed
 * array of specified [size].
 */
kintern int32_t
array_seq_len(int32_t size);

/*
 * Destroys the buffer contained in the array_t pointed by [array_].
 */
kintern bool
array_reserve(void *array, int32_t *size, int32_t *len,
   struct koji_allocator *alloc, int32_t elemsize, int32_t newlen);

/*
 * (internal) Resizes [array_] to contain its current size plus [num_elements].
 * Returns a pointer to the first of new elements.
 */
kintern bool
array_resize(void *array, int32_t *size, int32_t *len,
   struct koji_allocator *alloc, int32_t elemsize, int32_t newsize);

/*
 * Pushes [n] elements of specified [type] in array pointed by [parray] defined
 * using [array_type()]. Returns a typed pointer to the first element pushed.
 */
kintern void
array_free(void *array, int32_t *size, int32_t *len,
   struct koji_allocator *alloc, int32_t elemsize);

/*
 * Pushes one element of specified [type] in array pointed by [parray] defined
 * using [array_type()]. Returns a typed pointer to the element pushed.
 */
kintern void *
_array_push(void *array, int32_t *size, int32_t *len,
   struct koji_allocator *alloc, int32_t elemsize, int32_t count);

/*
 * Computes and returns the 64-bit hash of uint64 value [x]
 */
static
uint64_t mix64(uint64_t x)
{
   x = (x ^ (x >> 30)) * (uint64_t)(0xbf58476d1ce4e5b9);
   x = (x ^ (x >> 27)) * (uint64_t)(0x94d049bb133111eb);
   x = x ^ (x >> 31);
   return x;
}

/*
 * Computes the 64-bit Murmur2 hash of [key] data of [len] bytes using
 * specified [seed].
 */
kintern uint64_t
murmur2(const void * key, int32_t len, uint64_t seed);
