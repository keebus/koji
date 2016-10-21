/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_vm.h"
#include "kj_bytecode.h"
#include "kj_value.h"
#include <stdarg.h>

 /*------------------------------------------------------------------------------------------------*/
 /* static                                                                                         */
 /*------------------------------------------------------------------------------------------------*/
static value_t* vm_register(vm_t* vm, vm_frame_t* frame, int location)
{
   assert(frame->stack_base + location < (int)vm->value_stack_size);
   return vm->value_stack + frame->stack_base + location;
}

static value_t* vm_value(vm_t* vm, vm_frame_t* frame, int location)
{
   return location >= 0
      ? vm_register(vm, frame, location)
      : (assert(-location - 1 < (int)frame->proto->num_constants), frame->proto->constants - (location + 1));
}

static value_t vm_call_operator(vm_t* vm, object_t* object, klass_operator_type_t operator, value_t arg)
{
   klass_t* klass = object->klass;
   switch (klass->type)
   {
      case CLASS_TYPE_BUILTIN:
         return klass->data.builtin.operators[operator](object, arg);

      default: assert(!"unreachable"); return value_nil();
   }
}

/*------------------------------------------------------------------------------------------------*/
/* internal                                                                                       */
/*------------------------------------------------------------------------------------------------*/
kj_intern void vm_init(vm_t* vm, allocator_t allocator)
{
   vm->valid = true;
   vm->allocator = allocator;

   //table_init(&vm->globals.table, &s->allocator, KJ_TABLE_DEFAULT_CAPACITY);
   //vm->globals.references = 1;
   vm->frame_stack_ptr = 0;
   vm->frame_stack_size = 16;
   vm->frame_stack = kj_alloc(vm_frame_t, vm->frame_stack_size, &vm->allocator);

   vm->value_stack_ptr = 0;
   vm->value_stack_size = 16;
   vm->value_stack = kj_alloc(value_t, vm->value_stack_size, &vm->allocator);

   string_class_init(&vm->class_string);
}

kj_intern void vm_deinit(vm_t* vm)
{
   /* destroy all value on the stack */
   for (uint i = 0; i < vm->value_stack_ptr; ++i)
   {
      value_destroy(vm->value_stack + i, &vm->allocator);
   }
   kj_free(vm->value_stack, &vm->allocator);

   /* release prototype references */
   for (uint i = 0; i < vm->frame_stack_ptr; ++i)
   {
      prototype_release(vm->frame_stack[i].proto, &vm->allocator);
   }
   kj_free(vm->frame_stack, &vm->allocator);

   /* destroy the global table */
   // assert(vm->globals.references == 1); table_deinit(&vm->globals.table, vm->allocator);
}

kj_intern void vm_push_frame(vm_t * vm, prototype_t *proto, uint stack_base)
{
   /* bump up the number of prototype references as it is now referenced by the new frame */
   ++proto->references;

   /* bump up the frame stack pointer */
   const uint frame_ptr = vm->frame_stack_ptr++;

   /* resize the array if needed */
   if (vm->frame_stack_ptr > vm->frame_stack_size)
   {
      vm->frame_stack_size *= 2;
      vm->frame_stack = kj_realloc(vm->frame_stack, sizeof(vm_frame_t) * vm->frame_stack_size,
         kj_alignof(vm_frame_t), &vm->allocator);
   }

   /* set the new frame data */
   vm_frame_t* frame = &vm->frame_stack[frame_ptr];
   frame->proto = proto;
   frame->pc = 0;
   frame->stack_base = stack_base;

   /* push required locals in the value stack */
   for (int i = 0, n = proto->num_registers; i < n; ++i)
   {
      *vm_push(vm) = value_nil();
   }
}

kj_intern void vm_throwv(vm_t * vm, const char * format, va_list args)
{
   /* push the error string on the stack */
   *vm_push(vm) = value_new_stringfv(&vm->allocator, &vm->class_string, format, args);
   longjmp(vm->error_handler, 1);
}

kj_intern void vm_throw(vm_t * vm, const char * format, ...)
{
   va_list args;
   va_start(args, format);
   vm_throwv(vm, format, args);
   va_end(args);
}

kj_intern value_t * vm_top(vm_t * vm, int offset)
{
   int index = vm->value_stack_ptr + offset;
   assert((uint)index < vm->value_stack_ptr && "offset out of stack bounds.");
   return vm->value_stack + index;
}

kj_intern value_t * vm_push(vm_t * vm)
{
   const uint value_stack_ptr = vm->value_stack_ptr++;
   /*
    * Do we need to reallocate the value stack because it is not large enough?
    * Double the stack size and allocate a new buffer.
    */
   if (vm->value_stack_ptr > vm->value_stack_size)
   {
      vm->value_stack_size *= 2;
      vm->value_stack = kj_realloc(vm->value_stack, sizeof(value_t) * vm->value_stack_size,
         kj_alignof(value_t), &vm->allocator);
   }

   return vm->value_stack + value_stack_ptr;
}

kj_intern value_t vm_pop(vm_t * vm)
{
   value_t* value = vm_top(vm, -1);
   value_t retvalue = *value;
   *value = value_nil();
   --vm->value_stack_ptr;
   return retvalue;
}

kj_intern kj_result_t vm_resume(vm_t * vm)
{
   /* set the error handler so that if any runtime error occurs, we can cleanly return KOJI_ERROR
    * from this function */
   if (setjmp(vm->error_handler))
   {
      return KOJI_ERROR;
   }

   /* check state is valid, otherwise throw an error */
   if (!vm->valid)
   {
      vm_throw(vm, "cannot resume invalid state.");
   }

   /* declare important bookkeeping variable*/
   allocator_t *allocator = &vm->allocator;
   vm_frame_t* frame;
   instruction_t const *instructions;
   value_t *ra;
   value_t arg1, arg2;

new_frame: /* jumped to when a new frame is pushed onto the stack */
    /* check that some frame is on the stack, otherwise no function can be called, simply return
       success */
   if (vm->frame_stack_ptr == 0)
   {
      return KOJI_SUCCESS;
   }

   frame = vm->frame_stack + (vm->frame_stack_ptr - 1);
   instructions = frame->proto->instructions;

   for (;;)
   {
      instruction_t instr = instructions[frame->pc++];

      /* helper-shortcut macros */
      #define RA vm_register(vm, frame, decode_A(instr))
      #define ARG(x) vm_value(vm, frame, decode_##x(instr))

      switch (decode_op(instr))
      {
         case OP_LOADNIL:
            for (uint r = decode_A(instr), to = r + decode_Bx(instr); r < to; ++r)
               value_set_nil(vm_register(vm, frame, r), allocator);
            break;

         case OP_LOADBOOL:
            value_set_boolean(RA, allocator, (bool)decode_B(instr));
            frame->pc += decode_C(instr);
            break;

         case OP_MOV:
            value_set(RA, allocator, ARG(Bx));
            break;

         case OP_NEG:
            ra = RA;
            arg1 = *ARG(Bx);
            value_set_boolean(ra, allocator, !value_to_boolean(*ARG(Bx)));
            break;

         case OP_UNM:
            ra = RA;
            arg1 = *ARG(Bx);
            if (value_is_number(arg1))
            {
               value_set_number(ra, allocator, -arg1.number);
            }
            else if (value_is_object(arg1))
            {
               object_t* object = value_get_object(arg1);
               value_move(ra, allocator, vm_call_operator(vm, object, KLASS_OPERATOR_NEG, value_nil()));
            }
            else
            {
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
                  value_set_number(ra, allocator, (kj_number_t)(NUMBER_MODIFIER(arg1.number) op_ NUMBER_MODIFIER(arg2.number)));\
               }\
               else if (value_is_object(arg1))\
               {\
                  object_t *object = value_get_object(arg1);\
                  value_move(ra, allocator, vm_call_operator(vm, object, klassop, arg2));\
               }\
               else\
               {\
                  vm_throw(vm, "cannot apply binary operator " #name_ " between a %s and a %s.", value_type_str(arg1), value_type_str(arg2));\
               }\
               break;

         #define PASSTHROUGH(x) x
         #define CAST_TO_INT(x) ((long long)x)

         BINARY_OPERATOR(OP_ADD, +, add, KLASS_OPERATOR_ADD, PASSTHROUGH)
         BINARY_OPERATOR(OP_SUB, -, sub, KLASS_OPERATOR_SUB, PASSTHROUGH)
         BINARY_OPERATOR(OP_MUL, *, mul, KLASS_OPERATOR_MUL, PASSTHROUGH)
         BINARY_OPERATOR(OP_DIV, / , div, KLASS_OPERATOR_DIV, PASSTHROUGH)
         BINARY_OPERATOR(OP_MOD, %, mod, KLASS_OPERATOR_MOD, CAST_TO_INT)

         #undef BINARY_OPERATOR
         #undef PASSTHROUGH
         #undef CAST_TO_INT

         case OP_TESTSET:
         {
            /* if arg B to bool matches the boolean value in C, make the jump otherwise skip it */
            int newpc = frame->pc + 1;
            value_t const* arg = ARG(B);
            if (value_to_boolean(*arg) == decode_C(instr))
            {
               value_set(RA, allocator, arg);
               newpc += decode_Bx(instructions[frame->pc]);
            }
            frame->pc = newpc;
            break;
         }

         case OP_TEST:
         {
            int newpc = frame->pc + 1;
            if (value_to_boolean(*RA) == decode_Bx(instr))
            {
               newpc += decode_Bx(instructions[frame->pc]);
            }
            frame->pc = newpc;
            break;
         }

         case OP_JUMP:
            frame->pc += decode_Bx(instr);
            break;

         case OP_RET:
            return KOJI_SUCCESS;

         default:
            assert(!"Opcode not implemented.");
            break;
      }

      #undef RA
      #undef ARG
   }
}
