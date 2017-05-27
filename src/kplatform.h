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

#pragma warning(push, 0)
#include <assert.h>
#include <stdarg.h>
#pragma warning(pop)

#ifndef kintern
#define kintern
#endif

#ifdef _WIN64
#define KOJI_64
#endif

/*
 * Platform independent language extensions.
 */
#ifdef _MSC_VER

#define kbool _Bool
#define kalignof(T) __alignof(T)
#define kalloca(sz) _alloca(sz)
#define kinline __forceinline

typedef __int32 i8;
typedef __int32 i16;
typedef __int32 i32;
typedef __int64 i64;

typedef unsigned __int32 u8;
typedef unsigned __int32 u16;
typedef unsigned __int32 u32;
typedef unsigned __int64 u64;

#else

#define kbool unsigned char
#define kalignof(T) __alignof(T)
#define kalloca
#define kinline static __attribute__((inline))

#endif

/* define boole constants */
#define ktrue ((kbool)1)
#define kfalse ((kbool)0)

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

inline i32 min_i32(i32 a, i32 b) { return a < b ? a : b; }
inline i32 max_i32(i32 a, i32 b) { return a > b ? a : b; }

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
linear_alloc_create(struct koji_allocator *alloc, i32 size);

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
   i32 size, i32 align);


/*
 * A sequentially growing array is a simple array that is allowed to grow
 * dynamically one or more elements at a time. Initialize your array with
 * something like 'i32 *my_ints = NULL' and 'uint num_ints = 0'. Then call
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
 * Allocates a new sequential array with length the smallest power of two
 * greater than max(16, count). Only use array_seq_push on returned array.
 */
kintern void *
array_seq_new(struct koji_allocator *alloc, i32 elemsize, i32 count);

/*
 * Low-level function to grow a sequentially growable array. Used by higher
 * level functions [seqary_push] and [seqary_push_n], use those.
 */
kintern void *
array_seq_push_ex(void *arrayp, i32 *size, struct koji_allocator *alloc,
   i32 elemsize, i32 count);

/*
 * Frees sequential array arrayp with specified [size] num of elements
 * and [elemsize] element size.
 */
kintern void
array_seq_free(void *arrayp, i32 *size, struct koji_allocator *alloc,
   i32 elemsize);

/*
 * Returns the capacity (least greater power of two) of sequentially pushed
 * array of specified [size].
 */
kintern i32
array_seq_len(i32 size);

/*
 * Destroys the buffer contained in the array_t pointed by [array_].
 */
kintern kbool
array_reserve(void *array, i32 *size, i32 *len,
   struct koji_allocator *alloc, i32 elemsize, i32 newlen);

/*
 * (internal) Resizes [array_] to contain its current size plus [num_elements].
 * Returns a pointer to the first of new elements.
 */
kintern kbool
array_resize(void *array, i32 *size, i32 *len, struct koji_allocator *alloc,
   i32 elemsize, i32 newsize);

/*
 * Pushes [n] elements of specified [type] in array pointed by [parray] defined
 * using [array_type()]. Returns a typed pointer to the first element pushed.
 */
kintern void
array_free(void *array, i32 *size, i32 *len, struct koji_allocator *alloc,
   i32 elemsize);

/*
 * Pushes one element of specified [type] in array pointed by [parray] defined
 * using [array_type()]. Returns a typed pointer to the element pushed.
 */
kintern void *
_array_push(void *array, i32 *size, i32 *len, struct koji_allocator *alloc,
   i32 elemsize, i32 count);

/*
 * Computes and returns the 64-bit hash of uint64 value [x]
 */
static inline
u64 mix64(u64 x)
{
   x = (x ^ (x >> 30)) * (u64)(0xbf58476d1ce4e5b9);
   x = (x ^ (x >> 27)) * (u64)(0x94d049bb133111eb);
   x = x ^ (x >> 31);
   return x;
}

/*
 * Computes the 64-bit Murmur2 hash of [key] data of [len] bytes using
 * specified [seed].
 */
kintern u64
murmur2(const void * key, i32 len, u64 seed);
