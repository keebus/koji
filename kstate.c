/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kbytecode.h"
#include "kcompiler.h"
#include "kerror.h"
#include "kio.h"
#include "kmemory.h"

#include <string.h>

/*
 * koji_state definition
 */
struct koji_state {
	/* The memory allocator used for both compilation and execution. */
	struct koji_allocator allocator;
	/* Temporary buffer to hold the error message (TODO remove). */
	char temp_error_msg[512];
};

/*
 * Handles a compilation or runtime error by pushing the error message string
 * onto the stack.
 */
static void
handle_issue(struct sourceloc sloc, const char *message, void *user)
{
	struct koji_state *state = user;
	// koji_push_string(state, message, (int32_t)strlen(message));
	strcpy(state->temp_error_msg, message);
}

KOJI_API koji_state_t *
koji_create(struct koji_allocator *alloc)
{
#ifndef KOJI_NO_DEFAULT_ALLOCATOR
	if (!alloc)
		alloc = &s_default_allocator;
#endif

	if (!alloc)
		return NULL;

	koji_state_t *state = kalloc(koji_state_t, 1, alloc);
	if (!state)
		return NULL;

	state->allocator = *alloc;

	return state;
}

KOJI_API void
koji_delete(koji_state_t *state)
{
	kfree(state, 1, &state->allocator);
}

KOJI_API enum koji_result
koji_load(koji_state_t *state, struct koji_source *source)
{
	struct compile_info ci = {
	    .allocator = state->allocator,
	    .source = source,
	    .issue_handler.handle = handle_issue,
	    .issue_handler.user = state,
	    //  .cls_string = &state->vm.cls_string,
	};

	/* compile the source into a prototype */
	struct prototype *proto = NULL;
	enum koji_result result = compile(&ci, &proto);

	/* some error occurred and the prototype could not be compiled, report it
	/*/
	if (result)
		return result;

	module_dump(proto->module);

	module_unref(proto->module, &state->allocator);

	/* reset the num of references as the ref count will be increased when the
	 * prototype is referenced by a the new frame */
	// proto->module->refs = 0;

	/* create a closure to main prototype and push it to the stack */
	// vm_push_frame(&state->vm, proto, 0);

	return KOJI_OK;
}

KOJI_API enum koji_result
koji_load_string(koji_state_t *state, const char *source)
{
	struct koji_source src;
	koji_source_open_string("<string>", &source, &src);
	return koji_load(state, &src);
}

KOJI_API enum koji_result
koji_load_file(koji_state_t *state, const char *filename)
{
	/* try opening the file and report an error if file could not be open */
	struct koji_source src;
	if (!koji_source_open_file(filename, &src)) {
		// koji_push_stringf(state, "cannot open file '%s'.", filename);
		return KOJI_ERROR_COMPILATION;
	}
	/* load the file */
	enum koji_result r = koji_load(state, &src);
	koji_source_file_close(&src);
	return r;
}

KOJI_API const char *
koji_temporary_error(koji_state_t *state)
{
	return state->temp_error_msg;
}

//
// KOJI_API enum koji_result
// koji_run(koji_state_t *state)
//{
//	return vm_resume(&state->vm);
//}
//
// KOJI_API void
// koji_push_string(koji_state_t *state, const char *chars, int32_t len)
//{
//	struct string *str = string_new(&state->vm.cls_string, &state->allocator,
// len); 	memcpy(&str->chars, chars, len); 	str->chars[len] = 0;
//	*vm_push(&state->vm) = value_obj(str);
//}
//
// KOJI_API void
// koji_push_stringf(koji_state_t *state, const char *format, ...)
//{
//	va_list args;
//	va_start(args, format);
//	int32_t size = vsnprintf(0, 0, format, args);
//	struct string *str = string_new(&state->vm.cls_string, &state->allocator,
// size); 	vsnprintf(str->chars, size + 1, format, args); *vm_push(&state->vm)
// = value_obj(str); 	va_end(args);
//}
//
// KOJI_API const char *
// koji_string(koji_state_t *state, int32_t offset)
//{
//	union value value = *vm_top(&state->vm, offset);
//	if (!value_isobj(value))
//		return NULL;
//	struct string *str = value_getobjv(value);
//	if (str->object.class != &state->vm.cls_string)
//		return NULL;
//	return str->chars;
//}
//
// KOJI_API int32_t
// koji_string_length(koji_state_t *state, int32_t offset)
//{
//	union value value = *vm_top(&state->vm, offset);
//	if (!value_isobj(value))
//		return -1;
//	struct string *str = value_getobjv(value);
//	if (str->object.class != &state->vm.cls_string)
//		return -1;
//	return str->len;
//}
//
// KOJI_API void
// koji_pop(koji_state_t *state, int32_t n)
//{
//	vm_popn(&state->vm, n);
//}
