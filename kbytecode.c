/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kbytecode.h"

#include <stdio.h>
#include <string.h>

enum op_format {
	OP_FORMAT_UNKNOWN,
	OP_FORMAT_BX_OFFSET,
	OP_FORMAT_A_BX,
	OP_FORMAT_A_B,
	OP_FORMAT_A_B_C,
};

struct op_info {
	/* A string representation of this op code. */
	const char *name;
	/* This opcode format, mostly used for pretty printing instructions for
	 * debugging. */
	enum op_format format;
};

static struct op_info s_op_infos[] = {
    "loadnil", OP_FORMAT_A_BX,    "loadbool", OP_FORMAT_A_B_C,
    "mov",     OP_FORMAT_A_BX,    "neg",      OP_FORMAT_A_BX,
    "unm",     OP_FORMAT_A_BX,    "add",      OP_FORMAT_A_B_C,
    "sub",     OP_FORMAT_A_B_C,   "mul",      OP_FORMAT_A_B_C,
    "div",     OP_FORMAT_A_B_C,   "mod",      OP_FORMAT_A_B_C,
    "pow",     OP_FORMAT_A_B_C,   "testset",  OP_FORMAT_A_B_C,
    "closure", OP_FORMAT_A_BX,    "getglob",  OP_FORMAT_A_BX,
    "setglob", OP_FORMAT_A_BX,    "newtable", OP_FORMAT_A_BX,
    "get",     OP_FORMAT_A_B_C,   "this",     OP_FORMAT_UNKNOWN,
    "test",    OP_FORMAT_A_BX,    "jump",     OP_FORMAT_BX_OFFSET,
    "eq",      OP_FORMAT_A_B_C,   "lt",       OP_FORMAT_A_B_C,
    "lte",     OP_FORMAT_A_B_C,   "call",     OP_FORMAT_A_B_C,
    "mcall",   OP_FORMAT_UNKNOWN, "set",      OP_FORMAT_A_B_C,
    "ret",     OP_FORMAT_A_B,     "throw",    OP_FORMAT_A_BX,
    "debug",   OP_FORMAT_A_BX,
};

kintern void
module_unref(struct module *m, struct koji_allocator *alloc)
{
	if (--m->refs > 0)
		return;
	alloc->deallocate(m,
	                  module_sizeof(m->instrs_len, m->consts_len, m->protos_len),
	                  alloc->user);
}

static void
bytecode_dump(struct module const *m, instr_t const *instrs, uint32_t count,
              int32_t level)
{
	/* build a spacing str */
	int32_t margin_length = level * 3;
	char *margin = kalloca(margin_length + 1);
	for (int32_t i = 0, n = margin_length; i < n; ++i)
		margin[i] = ' ';
	margin[margin_length] = '\0';

	printf("%sbytecode:", margin);

	for (uint32_t i = 0; i < count; ++i) {
		instr_t instr = instrs[i];
		enum opcode op = decode_op(instr);
		int32_t regA = decode_A(instr);
		int32_t regB = decode_B(instr);
		int32_t regC = decode_C(instr);
		int32_t regBx = decode_Bx(instr);
		int32_t constant_reg = 0;
		int32_t offset = 0xffffffff;

		printf("%s%d) %s", margin, i + 1, s_op_infos[op].name);

		/* tabbing */
		for (int32_t j = 0, n = 10 - (int32_t)strlen(s_op_infos[op].name); j < n;
		     ++j)
			printf(" ");

		switch (s_op_infos[op].format) {
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
		/*if (constant_reg < 0) {
		   union value cnst = proto->consts[-constant_reg - 1];
		   printf("   ; ");

		   if (value_isnum(cnst)) {
		      printf("%f", cnst.num);
		   } else if (value_isobj(cnst)) {
		      struct string *str = (struct string *)value_getobj(cnst);
		      printf("\"%s\"", &str->chars);
		   }
		}*/

		if (offset != 0xffffffff) {
			printf("   ; to %d", offset + i + 2);
		}

		printf("\n");
	}

	/*for (int32_t i = 0; i < proto->nprotos; ++i) {
	   printf("\n");
	   prototype_dump(proto->protos[i], i, level + 1);
	}*/
}

kintern void
module_dump(struct module const *m)
{
}
