/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "kj_api.h"
#include <assert.h>

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
   #pragma warning(disable : 4204)
   #pragma warning(disable : 4221) /* cannot be initialized using address of automatic variable */
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

#include <stdarg.h>
#include <stdio.h>

#define snprintf c99_snprintf
#define vsnprintf c99_vsnprintf

inline int c99_vsnprintf(char *outBuf, size_t size, const char *format,
                         va_list ap)
{
  int count = -1;
  if (size != 0) count = _vsnprintf_s(outBuf, size, _TRUNCATE, format, ap);
  if (count == -1) count = _vscprintf(format, ap);
  return count;
}

inline int c99_snprintf(char *outBuf, size_t size, const char *format, ...)
{
  int count;
  va_list ap;
  va_start(ap, format);
  count = c99_vsnprintf(outBuf, size, format, ap);
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
#endif

#ifdef KOJI_AMALGAMATE
   #define kj_intern static
#else
   #define kj_intern
#endif

/*
 * Primitive types definitions for internal usage.
 */
typedef unsigned int uint;

/*
 */
static inline uint max_u(uint a, uint b) { return a > b ? a : b; }

/*
 * Structure used to implement an allocation strategy used by all koji operations (compilation and
 * execution for all memory allocation operations. Clients can specify their own allocation
 * strategy upon koji state creation. Every function has the same arguments of standard (aligned)
 * malloc, realloc and free plus a void* [userdata] arguments to provide those functions the
 * context for their operations.
 */
typedef struct {
   koji_malloc_fn malloc;
   koji_realloc_fn realloc;
   koji_free_fn free;
   void *userdata;
} allocator_t;

/*
 * Convenience macros that unwrap to koji allocator malloc/realloc/free call.
 */

#define kj_malloc(size, alignment, pallocator)                                                     \
   ((pallocator)->malloc((pallocator)->userdata, size, alignment))

/* Allocates an array of [count] elements of specified [type] */
#define kj_alloc(type, count, pallocator)                                                          \
   ((pallocator)->malloc((pallocator)->userdata, sizeof(type) * count, kj_alignof(type)))

#define kj_realloc(ptr, size, alignment, pallocator)                                               \
   ((pallocator)->realloc((pallocator)->userdata, ptr, size, alignment))

#define kj_free(ptr, pallocator) ((pallocator)->free((pallocator)->userdata, ptr))

/*
 * # Default allocation functions #
 * Allocation functions used if the user passes NULL when opening a koji state. They default to
 * system (aligned) malloc/realloc/free.
 */

kj_intern void * default_malloc(void*, koji_size_t size, koji_size_t alignment);
kj_intern void * default_realloc(void*, void *ptr, koji_size_t size, koji_size_t alignment);
kj_intern void   default_free(void*, void *ptr);

/*
 * # Sequentially growing arrays #
 * A sequentially growing array is a simple array that is allowed to grow dynamically one or more
 * elements at a time. Initialize your array with something like 'int* my_ints = NULL' and
 * 'uint num_ints = 0'. Then call seqarray_push() to increase the size of the array by one. This
 * function does not actually reallocate the array every time but allocates to the smallest power
 * of two elements large enough to contain the current number of elements. The minimum number of
 * elements is 16. Therefore when there are 16 elements and the 17th is being pushed, the array is
 * reallocated to have a capacity of 32 elements.
 */

/*
 * Grows the sequential (pointed) array [parray] of current (pointed) [psize] by one element of
 * specified [type] using specified [palloc] pointer to allocator.
 */
#define seqary_push(parray, psize, palloc, type)\
   ((type *)seqary_push_ex(parray, psize, palloc, sizeof(type), kj_alignof(type), 1))

/*
 * Grows the sequential (pointed) array [parray] of current (pointed) [psize] by [n] elements of
 * specified [type] using specified [palloc] pointer to allocator.
 */
#define seqary_push_n(parray, psize, palloc, type, n)\
   ((type *)seqary_push_ex(parray, psize, palloc, sizeof(type), kj_alignof(type), n))

/*
 * Low-level function to grow a sequentially growable array. Used by higher level functions
 * [seqary_push] and [seqary_push_n], use those.
 */
kj_intern void * seqary_push_ex(void *parray, uint *psize, allocator_t *alloc, uint elem_size,
                                uint elem_align, uint count);

/*
 * # Dynamic arrays #
 * Simple dinamically growable and shrinkable arrays. The difference between these arrays and the
 * sequentially growable arrays is that the latter simply compute the current capacity as the
 * smallest power of two larger than the current size. This version instead takes the capacity
 * explicitly so that the user can set the size to smaller values and no redundant realloc() will
 * be performed (capacity might already be large enough).
 */

/* Generates the declaration for a dynamic array of type @type */
#define array_type(type)                                                                           \
	struct {                                                                                         \
		type *data;                                                                                    \
		uint size;                                                                                     \
		uint capacity;                                                                                 \
	}

/* Generic definition of a dynamic array that points to void* */
typedef array_type(void) void_array;

/*
 * Makes sure that specified array at [array_] of elements of size [element_size] has enough
 * capacity to store [new_capacity] elements.
 */
kj_intern void array_reserve(void *array_, allocator_t *alloc, uint elem_size, uint elem_align,
                             uint new_capacity);

/*
 * Resizes specified dynamic array [array_] containing elements of size [elem_size] to a new number
 * of elements [new_size]. If the array has enough capacity to contain * [new_size] elements then
 * the function simply returns false. If the array instead is too small, the function allocates a
 * new buffer large enough to contain [new_size], copies the old buffer to the new one, destroys the
 * old buffer and returns true.
 */
kj_intern void array_resize(void *array_, allocator_t *alloc, uint elem_size, uint elem_align,
                            uint new_size);

/*
 * Destroys the buffer contained in the array_t pointed by [array_].
 */
kj_intern void array_free(void *array_, allocator_t *alloc);

/*
 * (internal) Resizes [array_] to contain its current size plus [num_elements]. Returns a pointer to
 * the first of new elements.
 */
kj_intern void *_array_push(void *array_, allocator_t *alloc, uint elem_size, uint elem_align,
                            uint count);

/*
 * Pushes [n] elements of specified [type] in array pointed by [parray] defined using
 * [array_type()]. Returns a typed pointer to the first element pushed.
 */
#define array_push_n(array, palloc, type, n)                                                       \
	((type *)_array_push(array, palloc, sizeof(type), kj_alignof(type), n))

/*
 * Pushes one element of specified [type] in array pointed by [parray] defined using [array_type()].
 * Returns a typed pointer to the element pushed.
 */
#define array_push(parray, palloc, type) array_push_n(parray, palloc, type, 1)
