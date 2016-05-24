/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_bytecode.h"
#include <malloc.h>
#include <stdio.h>

void prototype_dump(prototype_t const* proto, int level)
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

      printf("%d) %s\t\n", i + 1, OP_STRINGS[op]);
   }
}
