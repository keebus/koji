/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once
#ifndef _KOJI_H
#define _KOJI_H

#include <stdbool.h>
#include <stdint.h>

#define KOJI_API extern
#define KOJI_EOF (-1)

/*
 *
 */
enum koji_result {
	KOJI_OK = 0,
	KOJI_FAIL = -1,
	KOJI_OUT_OF_MEMORY = -2,
	KOJI_ERROR_COMPILATION = -3,
	KOJI_ERROR_RUNTIME = -4,
};

/*
 *
 */
typedef double koji_number_t;

/*
 *
 */
typedef struct koji_state koji_state_t;

/*
 * Interface to a customizable memory allocator.
 */
struct koji_allocator {
	/* Can be freely set by the user and is simply passed to all allocator
	 * functions in the `user` argument. */
	void *user;
	/* Todo */
	void *(*allocate)(uint32_t size, void *user);
	/* Tries to reallocate a chunk of memory pointed by `ptr` with size `oldsize`
	 * to `newsize` and return the address of the new memory allocation. */
	void *(*reallocate)(void *ptr, uint32_t oldsize, uint32_t newsize, void *user);
	/* Deallocates the memory allocation `size` bytes large at `ptr`. */
	void (*deallocate)(void *ptr, uint32_t totsize, void *user);
};

/*
 * The signature of the stream reading function used by koji to read a source
 * or bytecode file. The user will have to provide its own when they need to
 * have koji read input files from arbitrary streams (e.g. pack files, zipped
 * files, etc).
 * Implementers must read a byte from the stream and return it. If no more
 * bytes can be read, KOJI_EOF must be returned instead.
 */
typedef int32_t (*koji_source_read_t)(void *user);

/*
 * Wraps info about an input stream used for source reading.
 */
struct koji_source {
	/* The stream user data. */
	void *user;
	/* The stream name, used for identifying this stream in error reporting. */
	const char *name;
	/* The stream read function. */
	koji_source_read_t read;
};

/*
 *
 */
KOJI_API koji_state_t *
koji_create(struct koji_allocator *);

/*
 *
 */
KOJI_API void
koji_delete(koji_state_t *);

/*
 *
 */
KOJI_API enum koji_result
koji_load(koji_state_t *, struct koji_source *);

/*
 *
 */
KOJI_API enum koji_result
koji_load_string(koji_state_t *, const char *source);

/*
 *
 */
KOJI_API enum koji_result
koji_load_file(koji_state_t *, const char *filename);

/*
 * Temporary. Returns a string containing the error message if some operation
 * does not return KOJI_OK.
 */
KOJI_API const char *
koji_temporary_error(koji_state_t *);

#endif // _KOJI_H
