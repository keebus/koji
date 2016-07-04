/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#ifndef KOJI_HEADER
#define KOJI_HEADER

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Define basic int32_t and int64_t if this compiler does not support stdint.h.
 */
#if defined(_MSC_VER) && !defined(__clang__)
   typedef __int32 int32_t;
   typedef __int64 int64_t;
#else
   #include <stdint.h>
#endif

/*
 * Determine what KOJI_API stands for. If KOJI_STATIC is enabled, a static implementation of koji
 * is requested, i.e. the client is simply including the 'koji.c' file in some of its own
 * translation units.
 */
#ifdef KOJI_STATIC
   #define KOJI_API static
#elif defined(KOJI_DLL_EXPORT)
   #ifdef _MSC_VER
      #define KOJI_API __declspec(dllexport)
   #else
      #define KOJI_API /* todo */
   #endif
#elif defined(KOJI_DLL_IMPORT)
   #ifdef _MSC_VER
      #define KOJI_API __declspec(dllimport)
   #else
      #define KOJI_API /* todo */
   #endif
#else
   #define KOJI_API /* nothing, regular declaration with definition in 'koji.c' */
#endif

#if defined(_WIN64) || defined(__amd64__)
   #define KOJI_X64
#endif

/* Bunch of primitive type definitions. */

#ifdef KOJI_X64
   typedef unsigned long long koji_size_t;
#else
   typedef unsigned int koji_size_t;
#endif

/*
 * The 64-bit floating point type used by koji to represent numeric values.
 */
typedef double koji_number_t;

/*
 * Enumerates the possible results of various koji state operations such as load or executing
 * a script.
 */
typedef enum {
   KOJI_SUCCESS,
   KOJI_ERROR,
   KOJI_ERROR_OUT_OF_MEMORY,
} koji_result_t;

/*
 * Allocation functions used internally by koji for all allocation needs. They are optionally
 * specified by the user upon koji state creation. The internal default implementations of these
 * should the user not provide its own rely on system malloc/realloc/free.
 */
typedef void * (*koji_malloc_fn_t)  (void *userdata, koji_size_t size, koji_size_t alignment);
typedef void * (*koji_realloc_fn_t) (void *userdata, void *ptr, koji_size_t size,
                                   koji_size_t alignment);
typedef void   (*koji_free_fn_t)    (void *userdata, void *ptr);

/*
 * The signature of the stream reading function used by koji to read a source or bytecode file.
 * The user will have to provide its own when they need to have koji read input files from
 * arbitrary streams (e.g. pack files, zipped files, etc).
 * Implementers must read a byte from the stream and return it. If no more bytes can be read,
 * KOJI_EOF must be returned instead.
 */
typedef int (*koji_stream_read_t) (void* userdata);

/*
 * Value returned by any [koji_stream_read_t] function when no stream is exhausted.
 */
#define KOJI_EOF (-1)

/*
 * A koji state encapsulates all state needed by koji for script compilation and execution and is,
 * in fact, the target of all API operations. 
 */
typedef struct koji_state koji_state_t;

/*
 * Creates a new koji state with specified allocation functions. If any provided allocation function
 * is null, the corresponding default one (default malloc, realloc, etc.) will be used.
 * [alloc_userdata] is a pointer to a any user data that will be passed to the allocation functions
 * as the allocation context. All pointers can be NULL, in which case the default implementations
 * based on malloc/realloc/free will be used.
 */
KOJI_API
koji_state_t * koji_open(koji_malloc_fn_t malloc_fn, koji_realloc_fn_t realloc_fn, koji_free_fn_t free_fn,
                         void *alloc_userdata);

/*
 * #todo
 */
KOJI_API
koji_result_t koji_load(koji_state_t *, const char *source_name, koji_stream_read_t stream_read_fn,
                        void *stream_read_data);

/*
 * #todo
 */
KOJI_API koji_result_t koji_load_string(koji_state_t *state, const char *source);

/*
 * #todo
 */
KOJI_API koji_result_t koji_load_file(koji_state_t *state, const char *filename);

/*
 * Destroys an existing koji state, releasing all held references to values used by it and the
 * acquired memory and resources.
 */
KOJI_API void koji_close(koji_state_t *state);

/*
 * #todo
 */
KOJI_API koji_result_t koji_resume(koji_state_t *state);

#ifdef __cplusplus
} /* extern C */
#endif

#endif /* KOJI_HEADER */
