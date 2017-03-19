/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "kj_support.h"
#include "kj_value.h"
#include "kj_class.h"
#include "kj_table.h"
#include <setjmp.h>
#include <stdarg.h>

/*
 * Contains all the necessary information to run a script function (closure).
 */
struct vm_frame {
	/* the function prototype this frame is executing */
	struct prototype* proto;

	/* the program counter (current instruction index) */
	int pc;

	/* value stack base, i.e. the index of the first value in the stack for this frame */
	int stack_base;
};

enum vm_state {
	VM_STATE_INVALID,
	VM_STATE_VALID,
};

struct vm {
	struct koji_allocator allocator;

	/* whether the VM is in a valid state for execution (no error occurred) */
	enum vm_state valid;

	/* stack of activation frames, i.e. function calls */
	struct vm_frame* frame_stack;

	/* maximum elements capacity of the current frame stack allocation*/
	int frame_stack_size;

	/* frame pointer (== the actual number of frames in the stack) */
	int frame_stack_ptr;

	/* stack of local values (registers) */
	value_t* value_stack;

	/* maximum elements capacity of the current value stack allocation*/
	int value_stack_size;

	/* stack pointer */
	int value_stack_ptr;

	/* #documentation */
	jmp_buf error_handler;

	/* #documentation */
	struct class class_class;
	struct class class_string;
};

/*
 * Initializes or resets a VM.
 */
kj_intern void vm_init(struct vm*, struct koji_allocator allocator);

/*
 * Releases are resources owned by the VM.
 */
kj_intern void vm_deinit(struct vm*);

/*
 * Creates a new activation frame based on given prototype and pushes onto the stack. After this,
 * calling vm_continue() will begin executing specified prototype.
 */
kj_intern void          vm_push_frame(struct vm*, struct prototype* proto, int stack_base);
kj_intern void          vm_throwv(struct vm*, const char* format, va_list args);
kj_intern value_t*      vm_top(struct vm*, int offset);
kj_intern value_t*      vm_push(struct vm*);
kj_intern value_t       vm_pop(struct vm*);
kj_intern void          vm_popn(struct vm*, int n);
kj_intern koji_result_t vm_resume(struct vm*);
kj_intern void          vm_value_destroy(struct vm*, value_t value);
kj_intern uint64_t      vm_value_hash(struct vm*, value_t value);

static inline void vm_throw(struct vm* vm, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vm_throwv(vm, format, args);
	va_end(args);
}
