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

#if 0
 /* operations that write into R(A) */
  OP_LOADNIL,  /* loadnil A, Bx;    ; R(A), ..., R(Bx) = nil */
  OP_LOADBOOL, /* loadbool A, B, C  ; R(A) = bool(B) then jump by C */
  OP_MOV,      /* mov A, Bx         ; R(A) = R(Bx) */
  OP_NEG,      /* neg A, Bx         ; R(A) = not R(Bx) */
  OP_UNM,      /* unm A, Bx         ; R(A) = -R(Bx) */
  OP_ADD,      /* add A, B, C       ; R(A) = R(B) + R(C) */
  OP_SUB,      /* sub A, B, C       ; R(A) = R(B) - R(C) */
  OP_MUL,      /* mul A, B, C       ; R(A) = R(B) * R(C) */
  OP_DIV,      /* div A, B, C       ; R(A) = R(B) / R(C) */
  OP_MOD,      /* mod A, B, C       ; R(A) = R(B) % R(C) */
  OP_POW,      /* pow A, B, C       ; R(A) = pow(R(B), R(C)) */
  OP_TESTSET,  /* testset A, B, C   ; if R(B) == (bool)C then R(A) = R(B) else jump 1 */
  OP_CLOSURE,  /* closure A, Bx     ; R(A) = closure for prototype Bx */
  OP_GLOBALS,  /* globals A         ; get the global table into register A */
  OP_NEWTABLE, /* newtable A        ; creates a new table in R(A) */
  OP_GET,      /* get A, B, C       ; R(A) = R(B)[R(C)] */
  OP_THIS,     /* this A            ; R(A) = this */

  /* operations that do not write into R(A) */
  OP_TEST,     /* test A, Bx      ; if (bool)R(A) != (bool)B then jump 1 */
  OP_JUMP,     /* jump Bx         ; jump by Bx instructions */
  OP_EQ,       /* eq A, B, C      ; if (R(A) == R(B)) == (bool)C then nothing else jump 1 */
  OP_LT,       /* lt A, B, C      ; if (R(A) < R(B)) == (bool)C then nothing else jump 1 */
  OP_LTE,      /* lte A, B, C     ; if (R(A) <= R(B)) == (bool)C then nothing else jump 1 */
  OP_SCALL,    /* scall A, B, C   ; call static function at K[B] with C arguments starting from R(A) */
  OP_CALL,     /* call A, B, C    ; call closure R(B) with C arguments starting at R(A) */
  OP_MCALL,    /* mcall A, B, C   ; call object R(A - 1) method with name R(B) with C arguments from R(A) on */
  OP_SET,      /* set A, B, C     ; R(A)[R(B)] = R(C) */
  OP_RET,      /* ret A, B        ; return values R(A), ..., R(B)*/
#endif

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
   0, /* OP_LT */
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
      
      switch (OP_FORMATS[op]) {
         case OP_FORMAT_BX_OFFSET:
            printf("%d\t\t; to %d", regBx, regBx + i + 2);
            break;

         case OP_FORMAT_A_BX:
            printf("%d, %d", regA, regBx);
            constant_reg = regBx;
            break;

         case OP_FORMAT_A_B_C:
            printf("%d, %d, %d", regA, regB, regC);
            constant_reg = (regB < 0) ? regB : (regC < 0) ? regC : 0;

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

      printf("\n");
   }
}
