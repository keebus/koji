/*
 * koji - opcodes and bytecode manipulation
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kbytecode.h"
#include "kvalue.h"
#include "kstring.h"

#include <string.h>
#include <stdio.h>

enum op_format {
	OP_FORMAT_UNKNOWN,
	OP_FORMAT_BX_OFFSET,
	OP_FORMAT_A_BX,
   OP_FORMAT_A_B,
	OP_FORMAT_A_B_C,
};

static const enum op_format OP_FORMATS[] = {
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
   OP_FORMAT_A_BX, /* OP_CLOSURE */
   OP_FORMAT_A_BX, /* OP_GETGLOB */
   OP_FORMAT_A_BX, /* OP_SETGLOB */
   OP_FORMAT_A_BX, /* OP_NEWTABLE */
   OP_FORMAT_A_B_C, /* OP_GET */
   OP_FORMAT_UNKNOWN, /* OP_THIS */
   OP_FORMAT_A_BX, /* OP_TEST */
   OP_FORMAT_BX_OFFSET, /* OP_JUMP */
   OP_FORMAT_A_B_C, /* OP_EQ */
   OP_FORMAT_A_B_C, /* OP_LT */
   OP_FORMAT_A_B_C, /* OP_LTE */
   OP_FORMAT_A_B_C, /* OP_CALL */
   OP_FORMAT_UNKNOWN, /* OP_MCALL */
   OP_FORMAT_A_B_C, /* OP_SET */
   OP_FORMAT_A_B,  /* OP_RET */
   OP_FORMAT_A_BX, /* OP_THROW */
   OP_FORMAT_A_BX, /* OP_DEBUG */
};

kintern struct prototype *
prototype_new(int32_t nconsts, int32_t ninstrs, int32_t nprotos,
   struct koji_allocator *alloc)
{
   int instrs_offs = sizeof(struct prototype) + sizeof(union value) * nconsts;
   int protos_offs = instrs_offs + sizeof(instr_t) * ninstrs;
   int size = protos_offs;
   assert(size <= UINT16_MAX);

   struct prototype *proto = alloc->alloc(size, &alloc->alloc);
   proto->refs = 1;
   proto->size = (uint16_t)size;
   proto->ninstrs = ninstrs;
   proto->nconsts = nconsts;
   proto->nprotos = nprotos;
   proto->instrs = (void *)((char *)proto + instrs_offs);
   proto->protos = (void *)((char *)proto + protos_offs);
   return proto;
}

kintern void
prototype_release(struct prototype *proto, struct koji_allocator *alloc)
{
   if (--proto->refs == 0) {
      /* delete all child protos that reach reference to zero */
      for (int32_t i = 0, n = (int32_t)proto->nprotos; i < n; ++i)
         prototype_release(proto->protos[i], alloc);

      /* destroy constant values */
      for (int32_t i = 0, n = proto->nconsts; i < n; ++i)
         value_const_destroy(proto->consts[i], alloc);

      alloc->free(proto, proto->size, alloc);
   }
}

kintern void
prototype_dump(struct prototype const *proto, int32_t index, int32_t level)
{
   /* build a spacing str */
   int32_t margin_length = level * 3;
   char *margin = kalloca(margin_length + 1);
   for (int32_t i = 0, n = margin_length; i < n; ++i)
      margin[i] = ' ';
   margin[margin_length] = '\0';

   printf("%sprototype %d: "
          "#instructions %d, #constants %d, #locals %d, #prototypes %d\n",
          margin, index, proto->ninstrs, proto->nconsts, proto->nregs,
          proto->nprotos);

   for (int32_t i = 0; i < proto->ninstrs; ++i) {
      instr_t instr = proto->instrs[i];
      enum opcode op = decode_op(instr);
      int32_t regA = decode_A(instr);
      int32_t regB = decode_B(instr);
      int32_t regC = decode_C(instr);
      int32_t regBx = decode_Bx(instr);
      int32_t constant_reg = 0;
      int32_t offset = 0xffffffff;

      printf("%s%d) %s", margin, i + 1, OP_STRINGS[op]);

      /* tabbing */
      for (int32_t j = 0, n = 10 - (int32_t)strlen(OP_STRINGS[op]); j < n; ++j)
         printf(" ");

      switch (OP_FORMATS[op]) {
         case OP_FORMAT_BX_OFFSET:
            printf("%d   ", regBx);
            offset = regBx;
            break;

         case OP_FORMAT_A_BX:
            printf("%d, %d", regA, regBx);
            constant_reg = regBx;
            break;

         case OP_FORMAT_A_B:
            printf("%d, %d", regA, regB);
            break;

         case OP_FORMAT_A_B_C:
            printf("%d, %d, %d", regA, regB, regC);
            constant_reg = (regB < 0) ? regB : (regC < 0) ? regC : 0;
            break;

         default:
            break;
      }

      /* registers B and C might have consts, add a comment to the instruction
         to show the  constant value */
      if (constant_reg < 0) {
         union value cnst = proto->consts[-constant_reg - 1];
         printf("   ; ");

         if (value_isnum(cnst)) {
            printf("%f", cnst.num);
         }
         else if (value_isobj(cnst)) {
            struct string *str = value_getobjv(cnst);
            printf("\"%s\"", &str->chars);
         }
      }

      if (offset != 0xffffffff) {
         printf("   ; to %d", offset + i + 2);
      }

      printf("\n");
   }

   for (int i = 0; i < proto->nprotos; ++i) {
      printf("\n");
      prototype_dump(proto->protos[i], i, level + 1);
   }
}
