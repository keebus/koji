/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
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

#ifndef KOJI_H_
#define KOJI_H_

#define KOJI_API 

typedef enum {
   KOJI_OK = 0,
   KOJI_ERROR_OUT_OF_MEMORY = -1,
   KOJI_ERROR_COMPILE = -2,
   KOJI_ERROR_RUNTIME = -3
} koji_result_t;

struct koji_allocator {
   void *user;
   void *(*alloc)(int size, void *user);
   void *(*realloc)(void *ptr, int oldsize, int newsize, void *user);
   void  (*free)(void *ptr, int size, void *user);
};

typedef double koji_number_t;

/*
 * The signature of the stream reading function used by koji to read a source
 * or bytecode file. The user will have to provide its own when they need to
 * have koji read input files from arbitrary streams (e.g. pack files, zipped
 * files, etc).
 * Implementers must read a byte from the stream and return it. If no more
 * bytes can be read, KOJI_EOF must be returned instead.
 */
typedef int(*koji_source_read_t) (void *user);

/*
 * Wraps info about an input stream used for source reading.
 */
struct koji_source {
   const char *name; /* the stream name, used in error reporting */
   koji_source_read_t fn; /* the stream read function */
   void *user; /* the stream user data */
};

/*
 * Value returned by any [kstream_read_t] function when no stream is
 * exhausted.
 */
#define KOJI_EOF (-1)

/*
 * A koji state encapsulates all state needed by koji for script compilation
 * and execution and is, in fact, the target of all API operations.
 */
typedef struct koji_state koji_state_t;

KOJI_API koji_state_t *
koji_open(struct koji_allocator *alloc);

KOJI_API void
koji_close(koji_state_t*);

KOJI_API koji_result_t
koji_load(koji_state_t *state, struct koji_source *source);

KOJI_API koji_result_t
koji_load_string(koji_state_t *, const char *source);

KOJI_API koji_result_t
koji_load_file(koji_state_t *, const char *filename);

KOJI_API koji_result_t
koji_run(koji_state_t *);

KOJI_API void
koji_push_string(koji_state_t *, const char *source, int len);

KOJI_API void
koji_push_stringf(koji_state_t *, const char *format, ...);

KOJI_API const char *
koji_string(koji_state_t *, int offset);

KOJI_API int
koji_string_length(koji_state_t *, int offset);

KOJI_API void
koji_pop(koji_state_t *, int n);

#endif /* KOJI_H_ */
