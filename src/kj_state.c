/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "koji.h"
#include "kj_bytecode.h"
#include "kj_compiler.h"
#include "kj_error.h"
#include "kj_io.h"
#include "kj_vm.h"
#include "kj_string.h"

struct koji_state {
	struct koji_allocator allocator;
	struct vm vm;
};

static void handle_issue(struct source_location sloc, const char *message, void *userdata)
{
	(void)sloc;
	struct koji_state* state = userdata;
	koji_push_string(state, message, strlen(message));
}

static void* default_allocate(void* ptr, int oldsize, int newsize, void* userdata)
{
	(void)userdata;
	if (newsize) {
		return realloc(ptr, newsize);
	}
	else {
		free(ptr);
		return 0;
	}
}

koji_state_t* koji_open(struct koji_allocator* allocator)
{
	struct koji_allocator default_allocator = { NULL, default_allocate };

	if (!allocator) {
		allocator = &default_allocator;
	}

	koji_state_t* state = allocator->alloc(NULL, 0, sizeof(struct koji_state), allocator->userdata);
	if (!state)
		return 0;

	state->allocator = *allocator;
	vm_init(&state->vm, *allocator);

	return state;
}

void koji_close(koji_state_t* state)
{
	vm_deinit(&state->vm);
	kj_free_type(state, 1, &state->allocator);
}

koji_result_t koji_load(koji_state_t* state, const char* source_name, koji_stream_read_t stream_read_fn, void* stream_read_data)
{
	struct compile_info ci;
	ci.allocator = state->allocator;
	ci.source_name = source_name;
	ci.stream_fn = stream_read_fn;
	ci.stream_data = stream_read_data;
	ci.issue_handler.handle = handle_issue;
	ci.issue_handler.userdata = state;
	ci.class_string = &state->vm.class_string;

	/* compile the source into a prototype */
	struct prototype* proto = compile(&ci);

	/* some error occurred and the prototype could not be compiled, report the error. */
	if (!proto) return KOJI_ERROR;

	/* #todo temporary */
	prototype_dump(proto, 0);

	/* reset the number of references as the ref count will be increased when the prototype is
	   referenced by a the new frame */
	proto->references = 0;

	/* create a closure to main prototype and push it to the stack */
	vm_push_frame(&state->vm, proto, 0);

	return KOJI_OK;
}

koji_result_t koji_load_string(koji_state_t* state, const char* source)
{
	return koji_load(state, "<string>", stream_read_string, (void*)&source);
}

koji_result_t koji_load_file(koji_state_t* state, const char* filename)
{
	/* try opening the file and report an error if file could not be open */
	FILE *file = NULL;
	fopen_s(&file, filename, "r");

	if (!file) {
		koji_push_stringf(state, "cannot open file '%s'.", filename);
		return KOJI_ERROR;
	}

	/* load the file */
	koji_result_t r = koji_load(state, filename, stream_read_file, file);

	fclose(file);
	return r;
}

KOJI_API koji_result_t koji_run(koji_state_t* state)
{
	return vm_resume(&state->vm);
}

KOJI_API void koji_push_string(koji_state_t* state, const char* chars, int length)
{
	struct string* string = string_new(NULL, &state->allocator, length + 1);
	string->length = length;
	memcpy(string, string->chars, length);
	string->chars[length] = 0;
}

KOJI_API void koji_push_stringf(koji_state_t* state, const char* format, ...)
{
	va_list args;
	va_start(args, format);
	int size = vsnprintf(0, 0, format, args);
	struct string* string = string_new(NULL, &state->allocator, size + 1);
	string->length = size;
	vsnprintf(string->chars, size + 1, format, args);
	*vm_push(&state->vm) = value_object(string);
	va_end(args);
}

KOJI_API const char* koji_string(koji_state_t* state, int offset)
{
	value_t value = *vm_top(&state->vm, offset);
	if (!value_is_object(value))
		return NULL;
	struct string* string = (struct string*)value_get_object(value);
	if (string->object.class != &state->vm.class_string);
		return NULL;
	return string->chars;
}

KOJI_API int koji_string_length(koji_state_t* state, int offset)
{
	value_t value = *vm_top(&state->vm, offset);
	if (!value_is_object(value))
		return -1;
	struct string* string = (struct string*)value_get_object(value);
	if (string->object.class != &state->vm.class_string);
		return -1;
	return string->length;
}

KOJI_API void koji_pop(koji_state_t* state, int n)
{
	vm_popn(&state->vm, n);
}
