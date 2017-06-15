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
vm_value_setnil(struct koji_vm *vm, union value *val)
{
	vm_value_destroy(vm, *val);
	*val = value_nil();
}

static void
vm_value_setbool(struct koji_vm *vm, union value *val, bool b)
{
	vm_value_destroy(vm, *val);
	*val = value_bool(b);
}

static void
vm_value_setnum(struct koji_vm *vm, union value *val, koji_number_t num)
{
	vm_value_destroy(vm, *val);
	*val = value_num(num);
}

static union value *
vm_register(struct koji_vm *vm, struct koji_vm_frame *frame, int32_t loc)
{
	assert(frame->stackbase + loc < (int32_t)vm->valueslen);
	return vm->valuestack + frame->stackbase + loc;
}

static union value
vm_value(struct koji_vm *vm, struct koji_vm_frame *frame, int32_t loc)
{
	return *(loc >= 0
		? vm_register(vm, frame, loc)
		: (assert(-loc - 1 < (int32_t)frame->proto->nconsts),
         frame->proto->consts - (loc + 1)));
}

kintern void
vm_init(struct koji_vm *vm, struct koji_allocator *alloc)
{
	vm->validstate = true;
	vm->alloc = *alloc;

   /* init builtin classes */
   vm->class_class = class_class_new(alloc);
   vm->class_string = class_string_new(vm->class_class, alloc);
   vm->class_table = NULL;

   /* init table of globals */
   table_init(&vm->globals, alloc, 64);

	/* init frame stack */
	vm->framesp = 0;
	vm->frameslen = 16;
	vm->framestack = kalloc(struct koji_vm_frame, vm->frameslen, &vm->alloc);

	/* init value stack */
	vm->valuesp = 0;
	vm->valueslen = 16;
	vm->valuestack = kalloc(union value, vm->valueslen, &vm->alloc);
}

kintern void
vm_deinit(struct koji_vm *vm)
{
   int32_t i;

	/* destroy all values on the stack */
	for (i = 0; i < vm->valuesp; ++i) {
		vm_value_destroy(vm, vm->valuestack[i]);
	}
	kfree(vm->valuestack, vm->valueslen, &vm->alloc);

	/* release frame stack prototype references */
	for (i = 0; i < vm->framesp; ++i) {
		prototype_unref(vm->framestack[i].proto, &vm->alloc);
	}
	kfree(vm->framestack, vm->frameslen, &vm->alloc);

   /* release table of globals */
   table_deinit(&vm->globals, vm);

   /* release builtin classes */
   assert(vm->class_class->object.refs == 3);
   assert(vm->class_string->object.refs == 1);
   //assert(vm->class_table->object.refs == 1);
   class_free(vm->class_class, &vm->alloc);
   class_free(vm->class_string, &vm->alloc);
   //class_free(vm->class_table, &vm->alloc);
}

kintern void
vm_push_frame(struct koji_vm *vm, struct prototype *proto, int32_t stackbase)
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
	struct koji_vm_frame *frame = &vm->framestack[frame_ptr];
	frame->proto = proto;
	frame->pc = 0;
	frame->stackbase = stackbase;

	/* push required locals in the value stack */
	for (i = 0, n = proto->nregs; i < n; ++i)
		*vm_push(vm) = value_nil();
}

kintern void
vm_throwv(struct koji_vm *vm, const char *format, va_list args)
{
	/* push the error str on the stack */
	*vm_push(vm) = value_new_stringfv(vm->class_string, &vm->alloc, format, args);
	longjmp(vm->errorjmpbuf, 1);
}

kintern union value *
vm_top(struct koji_vm *vm, int32_t offset)
{
	int32_t index = vm->valuesp + offset;
	assert(index < vm->valuesp && "offset out of stack bounds.");
	return vm->valuestack + index;
}

kintern union value *
vm_push(struct koji_vm *vm)
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

kintern void
vm_pop(struct koji_vm *vm)
{
   assert(vm->valuesp > 0);
   vm_value_destroy(vm, vm->valuestack[--vm->valuesp]);
}

kintern void
vm_popn(struct koji_vm *vm, int32_t n)
{
   int32_t i;
	for (i = 0; i < n; ++i) {
		union value *value = vm_top(vm, -1);
		vm_value_destroy(vm, *value);
	}
	vm->valuesp -= n;
}

static __forceinline void
vm_callop(struct koji_vm *vm, struct object *obj, enum koji_op op,
   int32_t nargs, union value *dest, int32_t ndest)
{
   int32_t nret = obj->class->members[op].func(vm, obj + 1, op, nargs);
   int32_t nset = min_i32(nret, ndest);
   for (int32_t i = 0; i < nset; ++i)
      dest[i] = *vm_top(vm, -nret + i);
   for (int32_t i = nset; i < ndest; ++i)
      vm_value_setnil(vm, dest + i);
   vm_popn(vm, nret);
}

kintern koji_result_t
vm_resume(struct koji_vm *vm)
{
	/* helper-shortcut macros */
#define RA vm_register(vm, frame, decode_A(instr))
#define ARG(x) vm_value(vm, frame, decode_##x(instr))

   /* declare important bookkeeping variable*/
   struct koji_allocator *alloc = &vm->alloc;
   struct koji_vm_frame *frame;
   instr_t const *instrs;

	/* set the error handler so that if any runtime error occurs, we can cleanly
	 * return KOJI_ERROR from this function */
   if (setjmp(vm->errorjmpbuf)) {
      vm->validstate = VM_STATE_INVALID;
		return KOJI_ERROR_RUNTIME;
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
      int32_t compare, newpc;
      union value *ra;
      union value arg1, arg2;
		instr_t instr = instrs[frame->pc++];
      enum opcode op = decode_op(instr);

		switch (op) {
			case OP_LOADNIL:
				for (union value *r = RA, *re = r + decode_Bx(instr); r < re; ++r)
					vm_value_setnil(vm, r);
				break;

			case OP_LOADBOOL:
				vm_value_setbool(vm, RA, (bool)decode_B(instr));
				frame->pc += decode_C(instr);
				break;

			case OP_MOV:
				vm_value_set(vm, RA, ARG(Bx));
				break;

			case OP_NEG:
				vm_value_setbool(vm, RA, !value_tobool(ARG(Bx)));
				break;

			case OP_UNM:
				ra = RA;
				arg1 = ARG(Bx);
				if (value_isnum(arg1))
					vm_value_setnum(vm, ra, -arg1.num);
				else if (value_isobj(arg1))
               vm_callop(vm, value_getobj(arg1), KOJI_OP_UNM, 0, ra, 1);
				else
					vm_throw(vm, "cannot apply unary minus operation to a %s value.",
                  value_type_str(arg1));
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
                  vm_value_set(vm, vm_push(vm), arg2);\
                  vm_callop(vm, value_getobj(arg1), classop, 1, ra, 1);\
					}\
					else {\
						vm_throw(vm, "cannot apply binary operator " name_\
                     " between a %s and a %s.", value_type_str(arg1),\
                     value_type_str(arg2));\
					}\
					break;

#define PASSTHROUGH(x) x
#define CAST_TO_INT(x) ((int64_t)x)

				BINARY_OPERATOR(OP_ADD, +, "add",  KOJI_OP_ADD, PASSTHROUGH)
				BINARY_OPERATOR(OP_SUB, -, "sub",  KOJI_OP_SUB, PASSTHROUGH)
				BINARY_OPERATOR(OP_MUL, *, "mul",  KOJI_OP_MUL, PASSTHROUGH)
				BINARY_OPERATOR(OP_DIV, / , "div", KOJI_OP_DIV, PASSTHROUGH)
				BINARY_OPERATOR(OP_MOD, %, "mod",  KOJI_OP_MOD, CAST_TO_INT)

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

         case OP_GETGLOB:
            vm_value_set(vm, RA, table_get(&vm->globals, vm, ARG(Bx)));
            break;

			case OP_NEWTABLE:
				*RA = value_new_table(vm->class_table, &vm->alloc,
               TABLE_DEFAULT_CAPACITY);
				break;

			case OP_GET:
				arg1 = ARG(B);
				if (value_isobj(arg1)) {
               vm_value_set(vm, vm_push(vm), ARG(C));
               vm_callop(vm, value_getobj(arg1), KOJI_OP_GET, 1, RA, 1);
				}
				else {
					vm_throw(vm, "primitive type %s does not support `get` "
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
               vm_value_set(vm, vm_push(vm), ARG(C));\
               union value res;\
               vm_callop(vm, value_getobj(arg1), KOJI_OP_COMPARE, 1, &res, 1);\
               if (!value_isnum(res))\
                  vm_throw(vm, "comparison result must be a number.");\
               compare = res.num op_ 0;\
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

         case OP_CALL:
            ra = RA;
            arg1 = ARG(C);
            if (value_isobj(arg1)) {
               struct object *obj = value_getobj(arg1);
               int32_t nargs = decode_B(instr);
               int32_t sp = vm->valuesp;
               vm->valuesp = (int32_t)(ra - vm->valuestack) + nargs;
               obj->class->members[KOJI_OP_CALL].func(vm, obj + 1, KOJI_OP_CALL, nargs);
               vm->valuesp = sp;
            }
            break;

         case OP_SETGLOB:
            table_set(&vm->globals, vm, ARG(Bx), *RA);
            break;

         case OP_SET:
            ra = RA;
            if (value_isobj(*ra)) {
               vm_value_set(vm, vm_push(vm), ARG(B));
               vm_value_set(vm, vm_push(vm), ARG(C));
               vm_callop(vm, value_getobj(*ra), KOJI_OP_SET, 2, NULL, 0);
            }
            else {
               vm_throw(vm, "primitive type %s does not support `set` "
                  "operator.", value_type_str(arg1));
            }
            break;

			case OP_RET:
         {
            union value *dest = vm_register(vm, frame, 0);
            union value* dest_end = dest + frame->proto->nregs;
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
				vm->valuesp -= frame->proto->nregs;

				prototype_unref(frame->proto, alloc);
				goto new_frame;
         }

         case OP_THROW:
         {
            arg1 = ARG(Bx);
            struct string *str = value_getobjv(arg1);
            if (value_isobj(arg1) && str->object.class == vm->class_string)
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
					else if (value_getobj(*r)->class == vm->class_string)
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
vm_value_set(struct koji_vm *vm, union value *dest, union value src)
{
	union value old_dest = *dest;
	*dest = src;

	/* if value is an object, bump up its reference count */
	if (value_isobj(src))
		++value_getobj(src)->refs;

	vm_value_destroy(vm, old_dest);
}

kintern void
vm_object_unref(struct koji_vm *vm, struct object *obj)
{
entry:
	assert(obj && obj->refs > 0);
   if (--obj->refs > 0)
      return;

   struct class *cls = obj->class;
   struct object *cls_obj = &cls->object;
   int32_t nret = cls->members[KOJI_OP_DTOR]
                  .func(vm, obj + 1, KOJI_OP_DTOR, 0);
   assert(nret == 0);
   vm->alloc.free(obj, obj->size, vm->alloc.user);
   obj = cls_obj;
   goto entry;
}

kintern uint64_t
vm_value_hash(struct koji_vm *vm, union value val)
{
	if (value_isobj(val)) {
		struct object *obj = value_getobj(val);
      return obj->class->hash(obj);
	}
	else {
		return mix64(val.bits);
	}
}
