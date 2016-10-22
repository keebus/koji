/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "kj_value.h"
#include <setjmp.h>

struct prototype;

/*
 * Contains all the necessary information to run a script function (closure).
 */
typedef struct {
	/* the function prototype this frame is executing */
	struct prototype* proto;
	
	/* the program counter (current instruction index) */
	uint pc; 
	
	/* value stack base, i.e. the index of the first value in the stack for this frame */
	int stack_base;
	
} vm_frame_t;

/*
 * Write the #documentation.
 */
typedef struct vm {
   allocator_t allocator;

	/* whether the VM is in a valid state for execution (no error occurred) */
   bool valid;
    
	/* stack of activation frames, i.e. function calls */
	vm_frame_t *frame_stack;

	/* maximum elements capacity of the current frame stack allocation*/
	uint frame_stack_size;

   /* frame pointer (== the actual number of frames in the stack) */
	uint frame_stack_ptr;

	/* stack of local values (registers) */
	value_t *value_stack;

	/* maximum elements capacity of the current value stack allocation*/
	uint value_stack_size;
	
	/* stack pointer */
	uint value_stack_ptr;

   /* #documentation */
   jmp_buf error_handler;

   /* #documentation */
   klass_t class_string;
} vm_t;

/*
 * Initializes or resets a VM.
 */
kj_intern void vm_init(vm_t *vm, allocator_t allocator);

/*
 * Releases are resources owned by the VM.
 */
kj_intern void vm_deinit(vm_t *vm);

/*
 * Creates a new activation frame based on given prototype and pushes onto the stack. After this,
 * calling vm_continue() will begin executing specified prototype.
 */
kj_intern void vm_push_frame(vm_t *vm, struct prototype *proto, uint stack_base);

/*
 * Write the #documentation.
 */
kj_intern kj_result_t vm_resume(vm_t *vm);

/*
 * Write the #documentation.
 */
kj_intern void vm_throwv(vm_t *vm, const char *format, va_list args);

/*
 * Write the #documentation.
 */
kj_intern void vm_throw(vm_t *vm, const char *format, ...);

/*
 * Write the #documentation.
 */
kj_intern value_t* vm_top(vm_t *vm, int offset);

/*
 * Write the #documentation.
 */
kj_intern value_t* vm_push(vm_t *vm);

/*
 * Write the #documentation.
 */
kj_intern value_t vm_pop(vm_t* vm);
