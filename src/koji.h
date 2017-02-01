/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#ifndef KOJI_H_
#define KOJI_H_

#define KOJI_API

typedef enum {
	KOJI_OK = 0,
	KOJI_OUT_OF_MEMORY = -1,
	KOJI_ERROR = -2,
} koji_result_t;

struct koji_allocator {
	void* userdata;
	void* (*alloc)(void* ptr, int oldsize, int newsize, void* userdata);
};

typedef double koji_number_t;

/*
 * The signature of the stream reading function used by koji to read a source or bytecode file.
 * The user will have to provide its own when they need to have koji read input files from
 * arbitrary streams (e.g. pack files, zipped files, etc).
 * Implementers must read a byte from the stream and return it. If no more bytes can be read,
 * KOJI_EOF must be returned instead.
 */
typedef int (*koji_stream_read_t) (void* userdata);

/*
 * Value returned by any [kj_stream_read_t] function when no stream is exhausted.
 */
#define KOJI_EOF (-1)

/*
 * A koji state encapsulates all state needed by koji for script compilation and execution and is,
 * in fact, the target of all API operations. 
 */
typedef struct koji_state koji_state_t;

KOJI_API koji_state_t* koji_open(struct koji_allocator* allocator);
KOJI_API void          koji_close(koji_state_t*);
KOJI_API koji_result_t koji_load(koji_state_t *state, const char *source_name, koji_stream_read_t stream_read_fn, void *stream_read_data);
KOJI_API koji_result_t koji_load_string(koji_state_t*, const char* source);
KOJI_API koji_result_t koji_load_file(koji_state_t*, const char* filename);
KOJI_API koji_result_t koji_run(koji_state_t*);
KOJI_API void          koji_push_string(koji_state_t*, const char* string, int length);
KOJI_API void          koji_push_stringf(koji_state_t*, const char* format, ...);
KOJI_API const char*   koji_string(koji_state_t*, int offset);
KOJI_API int           koji_string_length(koji_state_t*, int offset);
KOJI_API void          koji_pop(koji_state_t*, int n);

#endif // KOJI_H_
