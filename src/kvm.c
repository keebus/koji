/*
 * koji scripting language
 * 
 * Copyright (C) 2017 Canio Massimo Tristano
 * 
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kvm.h"
#include "kbytecode.h"
#include "kstring.h"
#include "kclass.h"

#include <stdio.h> /* temp */

static void
vm_value_setnil(struct vm *vm, union value *val)
{
	vm_value_destroy(vm, *val);
	*val = value_nil();
}

static void
vm_value_setbool(struct vm *vm, union value *val, bool b)
{
	vm_value_destroy(vm, *val);
	*val = value_bool(b);
}

static void
vm_value_setnum(struct vm *vm, union value *val, koji_number_t num)
{
	vm_value_destroy(vm, *val);
	*val = value_num(num);
}

static union value *
vm_register(struct vm *vm, struct vm_frame *frame, int32_t loc)
{
	assert(frame->stackbase + loc < (int32_t)vm->valueslen);
	return vm->valuestack + frame->stackbase + loc;
}

static union value
vm_value(struct vm *vm, struct vm_frame *frame, int32_t loc)
{
	return *(loc >= 0
		? vm_register(vm, frame, loc)
		: (assert(-loc - 1 < (int32_t)frame->proto->nconsts),
         frame->proto->consts - (loc + 1)));
}

kintern void
vm_init(struct vm *vm, struct koji_allocator *alloc)
{
	vm->validstate = true;
	vm->alloc = *alloc;

	/* init frame stack */
	vm->framesp = 0;
	vm->frameslen = 16;
	vm->framestack = kalloc(struct vm_frame, vm->frameslen, &vm->alloc);

	/* init value stack */
	vm->valuesp = 0;
	vm->valueslen = 16;
	vm->valuestack = kalloc(union value, vm->valueslen, &vm->alloc);

	/* init builtin classes */
	class_builtin_init(&vm->cls_builtin);
	class_string_init(&vm->cls_string, &vm->cls_builtin);
	class_table_init(&vm->cls_table, &vm->cls_builtin);
}

kintern void
vm_deinit(struct vm *vm)
{
   int32_t i;

	/* destroy all values on the stack */
	for (i = 0; i < vm->valuesp; ++i) {
		vm_value_destroy(vm, vm->valuestack[i]);
	}
	kfree(vm->valuestack, vm->valueslen, &vm->alloc);

	/* release frame stack prototype references */
	for (i = 0; i < vm->framesp; ++i) {
		prototype_release(vm->framestack[i].proto, &vm->alloc);
	}
	kfree(vm->framestack, vm->frameslen, &vm->alloc);

   assert(vm->cls_builtin.object.refs == 4);
   assert(vm->cls_string.object.refs == 1);
   assert(vm->cls_table.object.refs == 1);
}

kintern void
vm_push_frame(struct vm *vm, struct prototype *proto, int32_t stackbase)
{
   int32_t i, n;

	/* bump up the num of prototype references as it is now referenced by the
	 * new frame */
	++proto->refs;

	/* bump up the frame stack pointer */
	const int32_t frame_ptr = vm->framesp++;

	/* resize the array if needed */
	if (vm->framesp > vm->frameslen) {
		int32_t newframeslen = vm->frameslen * 2;
		vm->framestack = krealloc(vm->framestack, vm->frameslen, newframeslen,
         &vm->alloc);
		vm->frameslen = newframeslen;
	}

	/* set the new frame data */
	struct vm_frame *frame = &vm->framestack[frame_ptr];
	frame->proto = proto;
	frame->pc = 0;
	frame->stackbase = stackbase;

	/* push required locals in the value stack */
	for (i = 0, n = proto->nlocals; i < n; ++i)
		*vm_push(vm) = value_nil();
}

kintern void
vm_throwv(struct vm *vm, const char *format, va_list args)
{
	/* push the error str on the stack */
	*vm_push(vm) = value_new_stringfv(&vm->cls_string, &vm->alloc, format, args);
	longjmp(vm->errorjmpbuf, 1);
}

kintern union value *
vm_top(struct vm *vm, int32_t offset)
{
	int32_t index = vm->valuesp + offset;
	assert(index < vm->valuesp && "offset out of stack bounds.");
	return vm->valuestack + index;
}

kintern union value *
vm_push(struct vm *vm)
{
	const int32_t valuesp = vm->valuesp++;
	
   /* do we need to reallocate the value stack because it is not large
	 * enough? */
	if (vm->valuesp > vm->valueslen) {
		int32_t newvalueslen = vm->valueslen * 2;
		vm->valuestack = krealloc(vm->valuestack, vm->valueslen, newvalueslen,
         &vm->alloc);
		vm->valueslen = newvalueslen;
	}

	return vm->valuestack + valuesp;
}

kintern union value
vm_pop(struct vm *vm)
{
	union value *value = vm_top(vm, -1);
	union value retvalue = *value;
	*value = value_nil();
	--vm->valuesp;
	return retvalue;
}

kintern void
vm_popn(struct vm *vm, int32_t n)
{
   int32_t i;
	for (i = 0; i < n; ++i) {
		union value *value = vm_top(vm, -1);
		vm_value_destroy(vm, *value);
	}
	vm->valuesp -= n;
}

kintern koji_result_t
vm_resume(struct vm *vm)
{
	/* helper-shortcut macros */
#define RA vm_register(vm, frame, decode_A(instr))
#define ARG(x) vm_value(vm, frame, decode_##x(instr))

   /* declare important bookkeeping variable*/
   struct koji_allocator *alloc = &vm->alloc;
   struct vm_frame *frame;
   instr_t const *instrs;

	/* set the error handler so that if any runtime error occurs, we can cleanly
	 * return KOJI_ERROR from this function */
   if (setjmp(vm->errorjmpbuf)) {
      vm->validstate = VM_STATE_INVALID;
		return KOJI_ERROR;
   }

	/* check state is valid, otherwise throw an error */
	if (vm->validstate == VM_STATE_INVALID)
		vm_throw(vm, "cannot resume invalid state.");

new_frame: /* jumped to when a new frame is pushed onto the stack */

	/* check that some frame is on the stack, otherwise no function can be
	 * called, simply return success */
	if (vm->framesp == 0)
		return KOJI_OK;

	frame = vm->framestack + (vm->framesp - 1);
	instrs = frame->proto->instrs;

	for (;;) {
      int32_t compare, newpc, reg, to_reg;
      union value *ra, arg1, arg2;
		instr_t instr = instrs[frame->pc++];

		switch (decode_op(instr)) {
			case OP_LOADNIL:
            reg = decode_A(instr);
            to_reg = reg + decode_Bx(instr);
				for (; reg < to_reg; ++reg)
					vm_value_setnil(vm, vm_register(vm, frame, reg));
				break;

			case OP_LOADBOOL:
				vm_value_setbool(vm, RA, (bool)decode_B(instr));
				frame->pc += decode_C(instr);
				break;

			case OP_MOV:
				vm_value_set(vm, RA, ARG(Bx));
				break;

			case OP_NEG:
				ra = RA;
				arg1 = ARG(Bx);
				vm_value_setbool(vm, ra, !value_tobool(ARG(Bx)));
				break;

			case OP_UNM:
				ra = RA;
				arg1 = ARG(Bx);
				if (value_isnum(arg1)) {
					vm_value_setnum(vm, ra, -arg1.num);
				}
				else if (value_isobj(arg1)) {
					struct object *obj = value_getobj(arg1);
					vm_value_destroy(vm, *ra);
					vm_value_set(vm, ra, obj->class->operator[CLASS_OP_UNM](vm, obj,
                  CLASS_OP_UNM, arg1, value_nil()).value);
				}
				else {
					vm_throw(vm, "cannot apply unary minus operation to a %s value.",
                  value_type_str(arg1));
				}
				break;

				/* binary operators */
#define BINARY_OPERATOR(case_, op_, name_, classop, NUMBER_MODIFIER)\
				case case_:\
					ra = RA;\
					arg1 = ARG(B);\
					arg2 = ARG(C);\
					if (value_isnum(arg1) && value_isnum(arg2)) {\
                  koji_number_t num = (koji_number_t)(\
                     NUMBER_MODIFIER(arg1.num) op_ NUMBER_MODIFIER(arg2.num));\
						vm_value_setnum(vm, ra, num);\
					}\
					else if (value_isobj(arg1)) {\
						struct object *obj = value_getobj(arg1);\
						vm_value_destroy(vm, *ra);\
						vm_value_set(vm, ra, obj->class->operator[classop](vm, obj,\
                     classop, arg1, arg2).value);\
					}\
					else {\
						vm_throw(vm, "cannot apply binary operator " name_ " between a %s and a %s.", value_type_str(arg1), value_type_str(arg2));\
					}\
					break;

#define PASSTHROUGH(x) x
#define CAST_TO_INT(x) ((int64_t)x)

				BINARY_OPERATOR(OP_ADD, +, "add", CLASS_OP_ADD, PASSTHROUGH)
				BINARY_OPERATOR(OP_SUB, -, "sub", CLASS_OP_SUB, PASSTHROUGH)
				BINARY_OPERATOR(OP_MUL, *, "mul", CLASS_OP_MUL, PASSTHROUGH)
				BINARY_OPERATOR(OP_DIV, / , "div", CLASS_OP_DIV, PASSTHROUGH)
				BINARY_OPERATOR(OP_MOD, %, "mod", CLASS_OP_MOD, CAST_TO_INT)

#undef PASSTHROUGH
#undef CAST_TO_INT
#undef BINARY_OPERATOR

			case OP_TESTSET:
				/* if arg B to bool matches the boole value in C, make the jump
               otherwise skip it */
				newpc = frame->pc + 1;
				union value const arg = ARG(B);
				if (value_tobool(arg) == decode_C(instr)) {
					vm_value_set(vm, RA, arg);
					newpc += decode_Bx(instrs[frame->pc]);
				}
				frame->pc = newpc;
				break;

			case OP_NEWTABLE:
				*RA = value_new_table(&vm->cls_table, &vm->alloc,
               TABLE_DEFAULT_CAPACITY);
				break;

			case OP_GET:
				arg1 = ARG(B);
				if (value_isobj(arg1)) {
					struct object *obj = value_getobj(arg1);
					vm_value_set(vm, RA, obj->class->operator[CLASS_OP_GET](vm, obj,
                  CLASS_OP_GET, ARG(C), value_nil()).value);
				}
				else {
					vm_throw(vm, "primitive type %s does not support `get` "
                  "operator.", value_type_str(arg1));
				}
				break;

			case OP_SET:
				ra = RA;
				arg1 = ARG(B);
				arg2 = ARG(C);
				if (value_isobj(*ra)) {
					struct object *obj = value_getobj(*ra);
					obj->class->operator[CLASS_OP_SET](vm, obj, CLASS_OP_SET, arg1,
                  arg2);
				}
				else {
					vm_throw(vm, "primitive type %s does not support `set` "
                  "operator.", value_type_str(arg1));
				}
				break;

			case OP_TEST:
				newpc = frame->pc + 1;
				if (value_tobool(*RA) == decode_Bx(instr)) {
					newpc += decode_Bx(instrs[frame->pc]);
				}
				frame->pc = newpc;
				break;

			case OP_JUMP:
				frame->pc += decode_Bx(instr);
				break;

#define COMPARISON_OPERATOR(case_, op_)\
				case case_:\
					ra = RA;\
					arg1 = ARG(B);\
					if (value_isnum(*ra) && value_isnum(arg1)) {\
						compare = ra->num op_ arg1.num;\
					}\
					else if (value_isobj(*ra)) {\
						struct object *obj = value_getobj(*ra);\
						compare = (obj->class->operator[CLASS_OP_COMPARE](vm,\
                     obj, CLASS_OP_COMPARE, arg1, value_nil()).compare op_ 0);\
					}\
					else {\
						compare = ra->bits op_ arg1.bits;\
					}\
					goto compare_done;

				COMPARISON_OPERATOR(OP_EQ, ==);
				COMPARISON_OPERATOR(OP_LT, <);
				COMPARISON_OPERATOR(OP_LTE, <=);

         compare_done:
            	newpc = frame->pc + 1;
					if (compare == decode_C(instr)) {
						newpc += decode_Bx(instrs[frame->pc]);
					}
					frame->pc = newpc;
               break;

#undef COMPARISON_OPERATOR

			case OP_RET:
         {
            union value *dest = vm_register(vm, frame, 0);
            union value* dest_end = dest + /* frame->proto->num_arguments +*/
               frame->proto->nlocals;
            union value* src = vm_register(vm, frame, decode_A(instr));
            union value* src_end = src + decode_Bx(instr);

				/* copy locals from starting from 0 from the current frame to the
               previous frame result locals */
            for (; src < src_end; ++src, ++dest) {
               vm_value_destroy(vm, *dest); /* make return reg nil */
               *dest = *src; /* move value over */
               *src = value_nil();
            }
            
				/* set all other current locals to nil */
				for (; dest < dest_end; ++dest) {
					vm_value_destroy(vm, *dest);
					*dest = value_nil();
				}

				/* pop the frame, release the prototype reference */
				vm->framesp -= 1;
				vm->valuesp -= frame->proto->nlocals;

				prototype_release(frame->proto, alloc);
				goto new_frame;
         }

         case OP_THROW:
         {
            arg1 = ARG(Bx);
            struct string *str = value_getobjv(arg1);
            if (value_isobj(arg1) && str->object.class == &vm->cls_string)
               vm_throw(vm, str->chars);
            else
               vm_throw(vm, "throw argument must be a string.");
            break;
         }
            
			case OP_DEBUG:
				printf("debug: ");
				for (union value *r = RA, *e = r + decode_Bx(instr); r < e; ++r) {
					if (value_isnil(*r))
                  printf("nil");
					else if (value_isbool(*r))
                  printf("%s", value_getbool(*r) ? "true" : "false");
					else if (value_isnum(*r))
                  printf("%f", r->num);
					else if (value_getobj(*r)->class == &vm->cls_string)
						printf("%s", &((struct string*)value_getobj(*r))->chars);
					else
						printf("<object:%p>", value_getobj(*r));
					printf(", ");
					vm_value_setnil(vm, RA);
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

kintern void
vm_value_set(struct vm *vm, union value *dest, union value src)
{
	union value old_dest = *dest;
	*dest = src;

	/* if value is an object, bump up its reference count */
	if (value_isobj(src))
		++value_getobj(src)->refs;

	vm_value_destroy(vm, old_dest);
}

kintern void
vm_object_unref(struct vm *vm, struct object *obj)
{
entry:
	assert(obj && obj->refs > 0);
   if (--obj->refs == 0) {
      struct object *cls_obj = &obj->class->object;
		obj->class->dtor(vm, obj);
      obj = cls_obj; 
      goto entry;
   }
}

kintern uint64_t
vm_value_hash(struct vm *vm, union value val)
{
	if (value_isobj(val)) {
		struct object *obj = value_getobj(val);
		return obj->class->operator[CLASS_OP_HASH](vm, obj, CLASS_OP_HASH,
         value_nil(), value_nil()).hash;
	}
	else {
		return mix64(val.bits);
	}
}
