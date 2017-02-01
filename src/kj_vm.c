/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_vm.h"
#include "kj_bytecode.h"
#include "kj_string.h"

static bool object_deref(struct object* object)
{
	if (!object) return false;
	return --object->references == 0;
}

static void value_destroy(struct vm* vm, value_t value)
{
	if (value_is_object(value)) {
		struct object* object = value_get_object(value);
		if (object_deref(object)) {
			if (object_deref((struct object*)object->class))
				kj_free_type(object->class, 1, &vm->allocator);
			kj_free_type(object, 1, &vm->allocator);
		}
	}
}

static void value_set_nil(struct vm* vm, value_t* value)
{
	value_destroy(vm, *value);
	*value = value_nil();
}

static void value_set_boolean(struct vm* vm, value_t* value, bool boolean)
{
	value_destroy(vm, *value);
	*value = value_boolean(boolean);
}

static void value_set_number(struct vm* vm, value_t* value, koji_number_t number)
{
	value_destroy(vm, *value);
	*value = value_number(number);
}

static void value_set(struct vm* vm, value_t* dest, value_t const* src)
{
	if (dest == src) return;

	value_destroy(vm, *dest);
	*dest = *src;

	/* if value is an object, bump up its reference count */
	if (value_is_object(*dest))
		++value_get_object(*dest)->references;
}

static value_t* vm_register(struct vm* vm, struct vm_frame* frame, int location)
{
	assert(frame->stack_base + location < (int)vm->value_stack_size);
	return vm->value_stack + frame->stack_base + location;
}

static value_t* vm_value(struct vm* vm, struct vm_frame* frame, int location)
{
	return location >= 0
		? vm_register(vm, frame, location)
		: (assert(-location - 1 < (int)frame->proto->num_constants), frame->proto->constants - (location + 1));
}

kj_intern void vm_init(struct vm* vm, struct koji_allocator allocator)
{
	vm->valid = true;
	vm->allocator = allocator;

	/* init frame stack */
	vm->frame_stack_ptr = 0;
	vm->frame_stack_size = 16;
	vm->frame_stack = kj_alloc_type(struct vm_frame, vm->frame_stack_size, &vm->allocator);

	/* init value stack */
	vm->value_stack_ptr = 0;
	vm->value_stack_size = 16;
	vm->value_stack = kj_alloc_type(value_t, vm->value_stack_size, &vm->allocator);
}

kj_intern void vm_deinit(struct vm* vm)
{
	/* destroy all values on the stack */
	for (int i = 0; i < vm->value_stack_ptr; ++i) {
		value_destroy(vm, vm->value_stack[i]);
	}
	kj_free_type(vm->value_stack, vm->value_stack_size, &vm->allocator);

	/* release frame stack prototype references */
	for (int i = 0; i < vm->frame_stack_ptr; ++i) {
		prototype_release(vm->frame_stack[i].proto, &vm->allocator);
	}
	kj_free_type(vm->frame_stack, vm->frame_stack_size, &vm->allocator);
}

kj_intern void vm_push_frame(struct vm* vm, struct prototype *proto, int stack_base)
{
	/* bump up the number of prototype references as it is now referenced by the new frame */
	++proto->references;

	/* bump up the frame stack pointer */
	const int frame_ptr = vm->frame_stack_ptr++;

	/* resize the array if needed */
	if (vm->frame_stack_ptr > vm->frame_stack_size) {
		int new_frame_stack_size = vm->frame_stack_size * 2;
		vm->frame_stack = kj_realloc_type(vm->frame_stack, vm->frame_stack_size, new_frame_stack_size, &vm->allocator);
		vm->frame_stack_size = new_frame_stack_size;
	}

	/* set the new frame data */
	struct vm_frame* frame = &vm->frame_stack[frame_ptr];
	frame->proto = proto;
	frame->pc = 0;
	frame->stack_base = stack_base;

	/* push required locals in the value stack */
	for (int i = 0, n = proto->num_registers; i < n; ++i)
		*vm_push(vm) = value_nil();
}

kj_intern void vm_throwv(struct vm* vm, const char *format, va_list args)
{
	/* push the error string on the stack */
	*vm_push(vm) = value_new_stringfv(NULL, &vm->allocator, format, args);
	longjmp(vm->error_handler, 1);
}

kj_intern value_t* vm_top(struct vm* vm, int offset)
{
	int index = vm->value_stack_ptr + offset;
	assert(index < vm->value_stack_ptr && "offset out of stack bounds.");
	return vm->value_stack + index;
}

kj_intern value_t* vm_push(struct vm* vm)
{
	const int value_stack_ptr = vm->value_stack_ptr++;
	/* do we need to reallocate the value stack because it is not large enough? */
	if (vm->value_stack_ptr > vm->value_stack_size) {
		int new_value_stack_size = vm->value_stack_size * 2;
		vm->value_stack = kj_realloc_type(vm->value_stack, vm->value_stack_size, new_value_stack_size, &vm->allocator);
		vm->value_stack_size = new_value_stack_size;
	}

	return vm->value_stack + value_stack_ptr;
}

kj_intern value_t vm_pop(struct vm* vm)
{
	value_t* value = vm_top(vm, -1);
	value_t retvalue = *value;
	*value = value_nil();
	--vm->value_stack_ptr;
	return retvalue;
}

kj_intern void vm_popn(struct vm* vm, int n)
{
	for (int i = 0; i < n; ++i) {
		value_t* value = vm_top(vm, -1);
		value_destroy(vm, *value);
	}
	vm->value_stack_ptr -= n;
}

kj_intern koji_result_t vm_resume(struct vm* vm)
{
	/* helper-shortcut macros */
#define RA vm_register(vm, frame, decode_A(instr))
#define ARG(x) vm_value(vm, frame, decode_##x(instr))

	/* set the error handler so that if any runtime error occurs, we can cleanly return KOJI_ERROR
	 * from this function
	 */
	if (setjmp(vm->error_handler))
		return KOJI_ERROR;

	/* check state is valid, otherwise throw an error */
	if (!vm->valid)
		vm_throw(vm, "cannot resume invalid state.");

	/* declare important bookkeeping variable*/
	struct koji_allocator* allocator = &vm->allocator;
	struct vm_frame* frame;
	instruction_t const* instructions;
	value_t *ra, arg1, arg2;

new_frame: /* jumped to when a new frame is pushed onto the stack */
	/* check that some frame is on the stack, otherwise no function can be called, simply return
	   success */
	if (vm->frame_stack_ptr == 0)
		return KOJI_OK;

	frame = vm->frame_stack + (vm->frame_stack_ptr - 1);
	instructions = frame->proto->instructions;

	for (;;) {
		instruction_t instr = instructions[frame->pc++];

		switch (decode_op(instr)) {
			case OP_LOADNIL:
				for (int r = decode_A(instr), to = r + decode_Bx(instr); r < to; ++r)
					value_set_nil(vm, vm_register(vm, frame, r));
				break;

			case OP_LOADBOOL:
				value_set_boolean(vm, RA, (bool)decode_B(instr));
				frame->pc += decode_C(instr);
				break;

			case OP_MOV:
				value_set(vm, RA, ARG(Bx));
				break;

			case OP_NEG:
				ra = RA;
				arg1 = *ARG(Bx);
				value_set_boolean(vm, ra, !value_to_boolean(*ARG(Bx)));
				break;

			case OP_UNM:
				ra = RA;
				arg1 = *ARG(Bx);
				if (value_is_number(arg1)) {
					value_set_number(vm, ra, -arg1.number);
				}
				/*else if (value_is_object(arg1)) {
					struct object* object = value_get_object(arg1);
					value_move(ra, allocator, vm_call_operator(vm, object, CLASS_OPERATOR_NEG, value_nil()));
				}*/
				else {
					vm_throw(vm, "cannot apply unary minus operation to a %s value.", value_type_str(arg1));
				}
				break;

				/* binary operators */
#define BINARY_OPERATOR(case_, op_, name_, klassop, NUMBER_MODIFIER)\
            case case_:\
               ra = RA;\
               arg1 = *ARG(B);\
               arg2 = *ARG(C);\
               if (value_is_number(arg1) && value_is_number(arg2))\
               {\
                  value_set_number(vm, ra, (koji_number_t)(NUMBER_MODIFIER(arg1.number) op_ NUMBER_MODIFIER(arg2.number)));\
               }\
               else\
               {\
                  vm_throw(vm, "cannot apply binary operator " #name_ " between a %s and a %s.", value_type_str(arg1), value_type_str(arg2));\
               }\
               break;
				/*else if (value_is_object(arg1))\
			   {\
				  object_t *object = value_get_object(arg1);\
				  value_move(ra, allocator, vm_call_operator(vm, object, klassop, arg2));\
			   }\*/

#define PASSTHROUGH(x) x
#define CAST_TO_INT(x) ((int64_t)x)

			BINARY_OPERATOR(OP_ADD, +, add, KLASS_OPERATOR_ADD, PASSTHROUGH)
			BINARY_OPERATOR(OP_SUB, -, sub, KLASS_OPERATOR_SUB, PASSTHROUGH)
			BINARY_OPERATOR(OP_MUL, *, mul, KLASS_OPERATOR_MUL, PASSTHROUGH)
			BINARY_OPERATOR(OP_DIV, / , div, KLASS_OPERATOR_DIV, PASSTHROUGH)
			BINARY_OPERATOR(OP_MOD, %, mod, KLASS_OPERATOR_MOD, CAST_TO_INT)

#undef PASSTHROUGH
#undef CAST_TO_INT
#undef BINARY_OPERATOR

			case OP_TESTSET: {
				/* if arg B to bool matches the boolean value in C, make the jump otherwise skip it */
				int newpc = frame->pc + 1;
				value_t const* arg = ARG(B);
				if (value_to_boolean(*arg) == decode_C(instr)) {
					value_set(vm, RA, arg);
					newpc += decode_Bx(instructions[frame->pc]);
				}
				frame->pc = newpc;
				break;
			}

			case OP_TEST: {
				int newpc = frame->pc + 1;
				if (value_to_boolean(*RA) == decode_Bx(instr)) {
					newpc += decode_Bx(instructions[frame->pc]);
				}
				frame->pc = newpc;
				break;
			}

			case OP_JUMP:
				frame->pc += decode_Bx(instr);
				break;

			case OP_RET: {
				int i = 0;

				/* copy locals from starting from 0 from the current frame to the previous frame result locals */
				for (int reg = decode_A(instr), to_reg = decode_Bx(instr); reg < to_reg; ++reg, ++i)
					value_set(vm, vm_register(vm, frame, i), vm_value(vm, frame, reg));
				
				/* set all other current locals to nil */
				for (int end = /* frame->proto->num_arguments + */ frame->proto->num_registers; i < end; ++i)
					value_destroy(vm, *vm_register(vm, frame, i));

				/* pop the frame, release the prototype reference */
				prototype_release(frame->proto, allocator);
				--vm->frame_stack_ptr;

				goto new_frame;
			}

			case OP_DEBUG:
				printf("debug: ");
				for (value_t* r = RA, *e = r + decode_Bx(instr); r < e; ++r) {
					if (value_is_nil(*r)) printf("nil");
					else if (value_is_boolean(*r)) printf("%s", value_get_boolean(*r) ? "true" : "false");
					else if (value_is_number(*r)) printf("%f", r->number);
					else {
						printf("<object:%p>", value_get_object(*r));
					}
					printf(", ");
				}
				printf("\n");
				break;

			default:
				assert(!"Opcode not implemented.");
				break;
		}
	}

#undef RA
#undef ARG
}
