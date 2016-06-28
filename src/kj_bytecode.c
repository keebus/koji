/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_bytecode.h"
#include "kj_value.h"
#include <malloc.h>
#include <stdio.h>
#include <string.h>

typedef enum {
   OP_FORMAT_UNKNOWN,
   OP_FORMAT_BX_OFFSET,
   OP_FORMAT_A_BX,
   OP_FORMAT_A_B_C,
} op_format_t;

static const op_format_t OP_FORMATS[] = {
   OP_FORMAT_A_BX, /* OP_LOADNIL */
   OP_FORMAT_A_B_C, /* OP_LOADBOOL */
   OP_FORMAT_A_BX, /* OP_MOV */
   OP_FORMAT_A_BX, /* OP_NEG */
   OP_FORMAT_A_BX, /* OP_UNM */
   OP_FORMAT_A_B_C, /* OP_ADD */
   OP_FORMAT_A_B_C, /* OP_SUB */
   OP_FORMAT_A_B_C, /* OP_MUL */
   OP_FORMAT_A_B_C, /* OP_DIV */
   OP_FORMAT_A_B_C, /* OP_MOD */
   OP_FORMAT_A_B_C, /* OP_POW */
   OP_FORMAT_A_B_C, /* OP_TESTSET */
   0, /* OP_CLOSURE */
   0, /* OP_GLOBALS */
   0, /* OP_NEWTABLE */
   0, /* OP_GET */
   0, /* OP_THIS */
   OP_FORMAT_A_BX, /* OP_TEST */
   OP_FORMAT_BX_OFFSET, /* OP_JUMP */
   OP_FORMAT_A_B_C, /* OP_EQ */
   OP_FORMAT_A_B_C, /* OP_LT */
   OP_FORMAT_A_B_C, /* OP_LTE */
   0, /* OP_SCALL */
   0, /* OP_CALL */
   0, /* OP_MCALL */
   0, /* OP_SET */
   0, /* OP_RET */
};

void prototype_dump(prototype_t const* proto, int level, class_t const *string_class)
{
   /* build a spacing string */
   uint margin_length = level * 3;
   char *margin = alloca(margin_length + 1);
   for (uint i = 0, n = margin_length; i < n; ++i)
      margin[i] = ' ';
   margin[margin_length] = '\0';

   printf("%sprototype \"%s\"\n#%sinstructions %d, #constants %d, #locals %d, #prototypes %d\n",
      margin, proto->name,
      margin, proto->num_instructions, proto->num_constants, proto->num_locals, 0);

   for (uint i = 0; i < proto->num_instructions; ++i) {
      instruction_t instr = proto->instructions[i];
      opcode_t op = decode_op(instr);
      int regA = decode_A(instr);
      int regB = decode_B(instr);
      int regC = decode_C(instr);
      int regBx = decode_Bx(instr);

      printf("%d) %s", i + 1, OP_STRINGS[op]);

      /* tabbing */
      for (uint j = 0, n = 10 - strlen(OP_STRINGS[op]); j < n; ++j) printf(" ");

      int constant_reg = 0;
      int offset = 0xffffffff;

      switch (OP_FORMATS[op]) {
         case OP_FORMAT_BX_OFFSET:
            printf("%d\t", regBx);
            offset = regBx;
            break;

         case OP_FORMAT_A_BX:
            printf("%d, %d", regA, regBx);
            constant_reg = regBx;
            break;

         case OP_FORMAT_A_B_C:
            printf("%d, %d, %d", regA, regB, regC);
            constant_reg = (regB < 0) ? regB : (regC < 0) ? regC : 0;
            break;

         default:
            break;
      }
      
      /* registers B and C might have constants, add a comment to the instruction to show the
       * constant value */
      if (constant_reg < 0) {
         printf("\t; ");
         value_t constant = proto->constants[-constant_reg - 1];
         if (value_is_number(constant)) {
            printf("%f", constant.number);
         }
         else if (value_is_object(constant)) {
            string_t *string = value_get_object(constant);
            assert(string->object.class == string_class); (void)string_class;
            printf("\"%s\"", string->chars);
         }
      }

      if (offset != 0xffffffff) {
         printf("\t; to %d", offset + i + 2);
      }

      printf("\n");
   }
}
