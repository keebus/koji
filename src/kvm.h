/*
 * koji scripting language
 * 
 * Copyright (C) 2017 Canio Massimo Tristano
 * 
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kplatform.h"
#include "kvalue.h"
#include "kclass.h"
#include "ktable.h"
#include <setjmp.h>
#include <stdarg.h>

struct prototype;

/*
 * Contains all the necessary information to run a script function (closure).
 */
struct koji_vm_frame {
	struct prototype *proto; /* function prototype this frame is executing */
	int32_t pc;        /* program counter (current instruction index) */
	int32_t stackbase; /* frame stack base, i.e. the index of the first value in
                     the stack for this frame invocation */
};

/*
 * Current VM state.
 */
enum vm_state {
	VM_STATE_INVALID,
	VM_STATE_VALID,
};

/*
 * The Virtual Machine.
 */
struct koji_vm {
	struct koji_allocator alloc; /* memory allocator */
   struct class *class_class;
   struct class *class_string;
   struct class *class_table;
   struct table globals;     /* table of globals */
	enum vm_state validstate; /* whether the VM is in a valid state for exec. */
	struct koji_vm_frame *framestack; /* stack of activation frames */
	int32_t framesp; /* frame stack pointer */
   int32_t frameslen; /* maximum elements capacity of the frame stack */
	union value *valuestack; /* stack of local values (registers) */
   int32_t valuesp; /* stack pointer */
	int32_t valueslen; /* maximum elements capacity of the current value stack */
	jmp_buf errorjmpbuf; /* #documentation */
};

/*
 * Initializes or resets a VM.
 */
kintern void
vm_init(struct koji_vm *, struct koji_allocator *alloc);

/*
 * Releases are resources owned by the VM.
 */
kintern void
vm_deinit(struct koji_vm *);

/*
 * Creates a new activation frame based on given prototype and pushes onto the
 * stack. After this, calling vm_continue() will begin executing specified
 * prototype.
 */
kintern void
vm_push_frame(struct koji_vm *, struct prototype *proto, int32_t stack_base);

kintern void
vm_throwv(struct koji_vm *, const char *format, va_list args);

static void
vm_throw(struct koji_vm *vm, const char *format, ...)
{
   va_list args;
   va_start(args, format);
   vm_throwv(vm, format, args);
   va_end(args);
}

kintern union value*
vm_top(struct koji_vm *, int32_t offset);

kintern union value*
vm_push(struct koji_vm *);

kintern void
vm_pop(struct koji_vm *);

kintern void
vm_popn(struct koji_vm *, int32_t n);

kintern koji_result_t
vm_resume(struct koji_vm *);

kintern void
vm_value_set(struct koji_vm *vm, union value *dest, union value src);

kintern void
vm_object_unref(struct koji_vm *, struct object*);

kintern bool
vm_value_equals(struct koji_vm *vm, union value v1, union value v2);

kintern uint64_t
vm_value_hash(struct koji_vm *, union value v);

static void
vm_value_destroy(struct koji_vm *vm, union value v)
{
	if (value_isobj(v)) {
		struct object *object = value_getobj(v);
		vm_object_unref(vm, object);
	}
}
