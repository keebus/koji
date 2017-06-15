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
 * Default issue handler: it pushes the error message to the vm vm stack.
 */
static void
handle_issue(struct sourceloc sloc, const char *message, void *user)
{
	(void)sloc;
	struct koji_vm *vm = user;
	koji_push_string(vm, message, (int)strlen(message));
}


KOJI_API koji_t
koji_open(struct koji_allocator *alloc)
{
	alloc = alloc ? alloc : default_alloc();
	if (!alloc)
		return NULL;

   struct koji_vm *vm = kalloc(struct koji_vm, 1, alloc);
   if (!vm)
		return NULL;

	vm->alloc = *alloc;
	vm_init(vm, alloc);

	return vm;
}

KOJI_API void
koji_close(koji_t vm)
{
	vm_deinit(vm);
	kfree(vm, 1, &vm->alloc);
}

KOJI_API koji_result_t
koji_load(koji_t vm, struct koji_source *source)
{
	struct compile_info ci;
	ci.alloc = vm->alloc;
	ci.source = source;
	ci.issue_handler.handle = handle_issue;
	ci.issue_handler.user = vm;
	ci.class_string = vm->class_string;

	/* compile the source into a prototype */
	struct prototype *proto = NULL;
   koji_result_t result = compile(&ci, &proto);

   /* some error occurred and the prototype could not be compiled, report the
	 * error. */
   if (result)
      return result;

	/* reset the num of references as the ref count will be increased when the
	 * prototype is referenced by a the new frame */
	proto->refs = 0;

	/* create a closure to main prototype and push it to the stack */
	vm_push_frame(vm, proto, 0);

	return KOJI_OK;
}

KOJI_API koji_result_t
koji_load_string(koji_t vm, const char *source)
{
   struct koji_source src;
   source_string_open(&src, "<string>", &source);
	return koji_load(vm, &src);
}

KOJI_API koji_result_t
koji_load_file(koji_t vm, const char *filename)
{
	/* try opening the file and report an error if file could not be open */
   struct koji_source src;
   if (!source_file_open(&src, filename)) {
      koji_push_stringf(vm, "cannot open file '%s'.", filename);
      return KOJI_ERROR_COMPILE;
   }
	/* load the file */
	koji_result_t r = koji_load(vm, &src);
	source_file_close(&src);
	return r;
}

KOJI_API
koji_result_t koji_run(koji_t vm)
{
	return vm_resume(vm);
}

KOJI_API koji_number_t
koji_number(koji_t vm, int offset)
{
   union value value = *vm_top(vm, offset);
   if (value_isnum(value))
      return value.num;
   else if (value_isbool(value))
      return value_getbool(value);
   else
      return 0;
}

KOJI_API const char *
koji_string(koji_t vm, int offset)
{
	union value value = *vm_top(vm, offset);
	if (!value_isobj(value))
		return NULL;
	struct string *str = value_getobjv(value);
	if (str->object.class != vm->class_string)
		return NULL;
	return str->chars;
}

KOJI_API int
koji_string_len(koji_t vm, int offset)
{
	union value value = *vm_top(vm, offset);
	if (!value_isobj(value))
		return -1;
	struct string *str = value_getobjv(value);
	if (str->object.class != vm->class_string)
		return -1;
	return string_len(str);
}

KOJI_API void
koji_push_string(koji_t vm, const char *chars, int len)
{
	struct string *str = string_new(vm->class_string, &vm->alloc, len);
	memcpy(&str->chars, chars, len);
	str->chars[len] = 0;
	*vm_push(vm) = value_obj(str);
}

KOJI_API void
koji_push_stringf(koji_t vm, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	int size = vsnprintf(0, 0, format, args);
	struct string *str = string_new(vm->class_string, &vm->alloc, size);
	vsnprintf(str->chars, size + 1, format, args);
	*vm_push(vm) = value_obj(str);
	va_end(args);
}

KOJI_API void
koji_push_class(koji_t vm, const char *name, int objsize,
   const char **members, int nmembers)
{
   *vm_push(vm) = value_obj(class_new(vm->class_class, name,
      (int32_t)strlen(name), objsize, members, nmembers, &vm->alloc));
}

KOJI_API koji_result_t
koji_class_set_op(koji_t vm, int offset, enum koji_op op,
   koji_function_t fn)
{
   union value v = *vm_top(vm, offset);
   if (!value_isobj(v))
      return KOJI_ERROR_INVALID;

   struct class *c = value_getobjv(v);
   if (c->object.class != vm->class_class)
      return KOJI_ERROR_INVALID;

   c->members[op].func = fn;
   return KOJI_OK;
}

KOJI_API koji_result_t 
koji_class_set_fn(koji_t vm, int offset, const char *member,
   koji_function_t fn)
{
   union value v = *vm_top(vm, offset);
   if (!value_isobj(v))
      return KOJI_ERROR_INVALID;

   struct class *c = value_getobjv(v);
   if (c->object.class != vm->class_class)
      return KOJI_ERROR_INVALID;
   
   struct class_member *m = class_getmember(c, member, (int32_t)strlen(member));
   if (!m)
      return KOJI_ERROR_INVALID;

   m->func = fn;
   return KOJI_OK;
}

KOJI_API void
koji_pop(koji_t vm, int n)
{
	vm_popn(vm, n);
}

KOJI_API void
koji_setglobal(koji_t vm, const char* key)
{
	union value keyv = value_new_stringf(vm->class_string, &vm->alloc, "%s", key);
	table_set(&vm->globals, vm, keyv, *vm_top(vm, -1));
	vm_pop(vm);
	value_const_destroy(keyv, &vm->alloc);
}
