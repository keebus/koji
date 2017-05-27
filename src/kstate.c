/*
 * koji scripting language
 * 
 * Copyright (C) 2017 Canio Massimo Tristano
 * 
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kbytecode.h"
#include "kcompiler.h"
#include "kerror.h"
#include "kio.h"
#include "kvm.h"
#include "kstring.h"

#include <string.h>
#include <stdio.h>

/*
 * Context for all koji operations.
 */
struct koji_state {
	struct koji_allocator alloc;  /* the allocator to use */
	struct vm vm;                 /* the virtual machine */
};

/*
 * Default issue handler: it pushes the error message to the state vm stack.
 */
static void
handle_issue(struct sourceloc sloc, const char *message, void *user)
{
	(void)sloc;
	struct koji_state *state = user;
	koji_push_string(state, message, strlen(message));
}


KOJI_API koji_state_t *
koji_open(struct koji_allocator *alloc)
{
	alloc = alloc ? alloc : default_alloc();
	if (!alloc)
		return NULL;

   struct koji_state *state = kalloc(struct koji_state, 1, alloc);
   if (!state)
		return NULL;

	state->alloc = *alloc;
	vm_init(&state->vm, alloc);

	return state;
}

KOJI_API void
koji_close(koji_state_t *state)
{
	vm_deinit(&state->vm);
	kfree(state, 1, &state->alloc);
}

KOJI_API koji_result_t
koji_load(koji_state_t *state, struct koji_source *source)
{
	struct compile_info ci;
	ci.alloc = state->alloc;
	ci.source = source;
	ci.issue_handler.handle = handle_issue;
	ci.issue_handler.user = state;
	ci.cls_string = &state->vm.cls_string;

	/* compile the source into a prototype */
	struct prototype *proto = compile(&ci);

	/* some error occurred and the prototype could not be compiled, report the
	 * error. */
	if (!proto) return KOJI_ERROR;

	/* #todo temporary */
	prototype_dump(proto, 0);

	/* reset the num of references as the ref count will be increased when the
	 * prototype is referenced by a the new frame */
	proto->refs = 0;

	/* create a closure to main prototype and push it to the stack */
	vm_push_frame(&state->vm, proto, 0);

	return KOJI_OK;
}

KOJI_API koji_result_t
koji_load_string(koji_state_t *state, const char *source)
{
   struct koji_source src;
   source_string_open(&src, "<string>", &source);
	return koji_load(state, &src);
}

KOJI_API koji_result_t
koji_load_file(koji_state_t *state, const char *filename)
{
	/* try opening the file and report an error if file could not be open */
   struct koji_source src;
   if (!source_file_open(&src, filename)) {
      koji_push_stringf(state, "cannot open file '%s'.", filename);
      return KOJI_ERROR;
   }
	/* load the file */
	koji_result_t r = koji_load(state, &src);
	source_file_close(&src);
	return r;
}

KOJI_API
koji_result_t koji_run(koji_state_t *state)
{
	return vm_resume(&state->vm);
}

KOJI_API void
koji_push_string(koji_state_t *state, const char *chars, int len)
{
	struct string *str = string_new(&state->vm.cls_string, &state->alloc, len);
	memcpy(&str->chars, chars, len);
	(&str->chars)[len] = 0;
	*vm_push(&state->vm) = value_obj(str);
}

KOJI_API void
koji_push_stringf(koji_state_t *state, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int size = vsnprintf(0, 0, format, args);
	struct string *str = string_new(&state->vm.cls_string, &state->alloc, size);
	vsnprintf((&str->chars), size + 1, format, args);
	*vm_push(&state->vm) = value_obj(str);
	va_end(args);
}

KOJI_API const char *
koji_string(koji_state_t *state, int offset)
{
	union value value = *vm_top(&state->vm, offset);
	if (!value_isobj(value))
		return NULL;
	struct string *str = value_getobjv(value);
	if (str->object.class != &state->vm.cls_string)
		return NULL;
	return &str->chars;
}

KOJI_API int
koji_string_length(koji_state_t *state, int offset)
{
	union value value = *vm_top(&state->vm, offset);
	if (!value_isobj(value))
		return -1;
	struct string *str = value_getobjv(value);
	if (str->object.class != &state->vm.cls_string)
		return -1;
	return str->len;
}

KOJI_API void
koji_pop(koji_state_t *state, int n)
{
	vm_popn(&state->vm, n);
}
