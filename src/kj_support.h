/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "koji.h"
#include <assert.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

/* add compiler specific (useless) warning ignores */
#if defined(__GNUC__)
   #pragma GCC diagnostic push
   #pragma GCC diagnostic ignored "-Wmultichar"
   /* unfortunately both GCC and clang erroneously report the standard C99 "{ 0 }" initialization
    * for a structure value as incomplete initialization. The following two pragmas ignore wrong
    * warnings with this construct. */
   #pragma GCC diagnostic ignored "-Wmissing-braces"
   #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
   
#elif defined(__clang__)
   #pragma clang diagnostic push
   #pragma clang diagnostic ignored "-Wmultichar"
   /* unfortunately both GCC and clang erroneously report the standard C99 "{ 0 }" initialization
    * for a structure value as incomplete initialization. The following two pragmas ignore wrong
    * warnings with this construct. */
   #pragma clang diagnostic ignored "-Wmissing-braces"
   #pragma clang diagnostic ignored "-Wmissing-field-initializers"
   
#elif defined(_MSC_VER)
   #pragma warning(push)
   #pragma warning(disable : 4200) /* disable warning for C99 flexible array members */
   #pragma warning(disable : 4204)
   #pragma warning(disable : 4221) /* cannot be initialized using address of automatic variable */
#endif

#ifdef _WIN64
#	define KOJI_64
#endif

/*
 * Platform independent language extensions.
 */
#define kj_alignof(T) __alignof(T)

/*
 * Fallback definitions and functions for Visual Studio < 2015.
 */
#if defined(_MSC_VER) && _MSC_VER < 1900

#define inline __inline

#define snprintf c99_snprintf
#define vsnprintf c99_vsnprintf

inline int c99_vsnprintf(char *out_buf, size_t size, const char *format, va_list ap)
{
  int count = -1;
  if (size != 0) count = _vsnprintf_s(out_buf, size, _TRUNCATE, format, ap);
  if (count == -1) count = _vscprintf(format, ap);
  return count;
}

inline int c99_snprintf(char *out_buf, size_t size, const char *format, ...)
{
  int count;
  va_list ap;
  va_start(ap, format);
  count = c99_vsnprintf(out_buf, size, format, ap);
  va_end(ap);
  return count;
}

/* define boolean type and constants */
#ifndef bool
#define bool unsigned char
#define true (1)
#define false (0)
#endif

#else
#include <stdbool.h>
#include <stdio.h>
#endif

#ifndef kj_intern
#define kj_intern
#endif

#ifdef _MSC_VER

typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;

typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;

#define kj_forceinline static __forceinline

#else

#include <stdint.h>

#define kj_forceinline static __attribute__((inline))

#endif

/*
 * Convenience macros that unwrap to koji allocator malloc/realloc/free call.
 */
/* Allocates an array of [count] elements of specified [type] */
#define kj_alloc_type(type, count, pallocator)\
   ((pallocator)->alloc(NULL, 0, sizeof(type) * count, (pallocator)->userdata))

#define kj_realloc_type(ptr, oldcount, count, pallocator)\
   ((pallocator)->alloc(ptr, sizeof(*ptr) * oldcount, sizeof(*ptr) * count, (pallocator)->userdata))

#define kj_free_type(ptr, count, pallocator)\
	((pallocator)->alloc(ptr, (sizeof *ptr) * count, 0, (pallocator)->userdata))


inline int max_i(int a, int b) { return a > b ? a : b; }

/*
 * A linear allocator provides a way to allocate memory linearly (thus quickly) and then clearing it
 * up later on when all allocations are not needed. Use it to freely allocate scratch memory that is
 * soon going to be freed altogether.
 */

/*
 * The minimum size of a linear allocator page in bytes.
 */
#define LINEAR_ALLOCATOR_PAGE_MIN_SIZE 1024

/*
 * A linear allocator structure. Clients are not required to use any
 */
typedef struct linear_allocator_page linear_allocator_t;

/*
 * Creates a new linear allocator of specified size (which will be max'ed with
 * LINEAR_ALLOCATOR_PAGE_MIN_SIZE). Use [allocator] for page allcations.
 */
kj_intern linear_allocator_t *linear_allocator_create(struct koji_allocator* allocator, int size);

/*
 * Destroys the linear allocator [talloc] which was created with specified [allocator]
 */
kj_intern void linear_allocator_destroy(linear_allocator_t* talloc, struct koji_allocator* allocator);

/*
 * Resets all current pages of specified linear allocator [talloc]. All pages after the first one
 * are deallocated.
 */
kj_intern void linear_allocator_reset(linear_allocator_t** talloc, struct koji_allocator* allocator);

/*
 * Allocates a chunk of memory of given [size] and [alignment] from specified linear allocator
 * [talloc] using [allocator] if a new page needs to be allocate.
 */
kj_intern void* linear_allocator_alloc(linear_allocator_t** talloc, struct koji_allocator* allocator, int size, int alignment);


/*
 * A sequentially growing array is a simple array that is allowed to grow dynamically one or more
 * elements at a time. Initialize your array with something like 'int* my_ints = NULL' and
 * 'uint num_ints = 0'. Then call seqarray_push() to increase the size of the array by one. This
 * function does not actually reallocate the array every time but allocates to the smallest power
 * of two elements large enough to contain the current number of elements. The minimum number of
 * elements is 16. Therefore when there are 16 elements and the 17th is being pushed, the array is
 * reallocated to have a capacity of 32 elements.
 */

/*
 * Grows the sequential (pointed) array [parray] of current (pointed) [psize] by [n] elements of
 * specified [type] using specified [palloc] pointer to allocator.
 */
#define array_push_seq(pparray, psize, palloc, type, n)\
   ((type *)array_push_seq_ex(pparray, psize, palloc, sizeof(type), n))

/*
 * Pushes [n] elements of specified [type] in array pointed by [parray] defined using
 * [array_type()]. Returns a typed pointer to the first element pushed.
 */
#define array_push(pparray, psize, pcapacity, palloc, type, n)\
	((type *)_array_push(pparray, psize, pcapacity, palloc, sizeof(type), n))

/*
 * Low-level function to grow a sequentially growable array. Used by higher level functions
 * [seqary_push] and [seqary_push_n], use those.
 */
kj_intern void* array_push_seq_ex(void* pparray, int* psize, struct koji_allocator* alloc, int elem_size, int count);

/*
 * Returns the capacity (least greater power of two) of sequentially pushed array of specified [size].
 */
kj_intern int array_seq_capacity(int size);

/*
 * Destroys the buffer contained in the array_t pointed by [array_].
 */
kj_intern bool array_reserve(void* parray, int* psize, int* pcapacity, struct koji_allocator* alloc, int elem_size, int new_capacity);

/*
 * (internal) Resizes [array_] to contain its current size plus [num_elements]. Returns a pointer to
 * the first of new elements.
 */
kj_intern bool array_resize(void* parray, int* psize, int* pcapacity, struct koji_allocator* alloc, int elem_size, int new_size);

/*
 * Pushes [n] elements of specified [type] in array pointed by [parray] defined using
 * [array_type()]. Returns a typed pointer to the first element pushed.
 */
kj_intern void array_free(void* parray, int* psize, int* pcapacity, struct koji_allocator* alloc);

/*
 * Pushes one element of specified [type] in array pointed by [parray] defined using [array_type()].
 * Returns a typed pointer to the element pushed.
 */
kj_intern void* _array_push(void* parray, int* psize, int* pcapacity, struct koji_allocator* alloc, int elem_size, int num_elements);

/*
 * Computes and returns the 64-bit hash of uint64 value [x]
 */
static inline uint64_t mix64(uint64_t x)
{
	x = (x ^ (x >> 30)) * (uint64_t)(0xbf58476d1ce4e5b9);
	x = (x ^ (x >> 27)) * (uint64_t)(0x94d049bb133111eb);
	x = x ^ (x >> 31);
	return x;
}

/*
 * Computes the 64-bit Murmur2 hash of [key] data of [len] bytes using specified [seed].
 */
kj_intern uint64_t murmur2(const void * key, int len, uint64_t seed);