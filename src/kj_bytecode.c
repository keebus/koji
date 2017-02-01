/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_bytecode.h"
#include "kj_value.h"
#include "kj_string.h"

enum op_format {
	OP_FORMAT_UNKNOWN,
	OP_FORMAT_BX_OFFSET,
	OP_FORMAT_A_BX,
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
   OP_FORMAT_UNKNOWN, /* OP_CLOSURE */
   OP_FORMAT_UNKNOWN, /* OP_GLOBALS */
   OP_FORMAT_UNKNOWN, /* OP_NEWTABLE */
   OP_FORMAT_UNKNOWN, /* OP_GET */
   OP_FORMAT_UNKNOWN, /* OP_THIS */
   OP_FORMAT_A_BX, /* OP_TEST */
   OP_FORMAT_BX_OFFSET, /* OP_JUMP */
   OP_FORMAT_A_B_C, /* OP_EQ */
   OP_FORMAT_A_B_C, /* OP_LT */
   OP_FORMAT_A_B_C, /* OP_LTE */
   OP_FORMAT_UNKNOWN, /* OP_SCALL */
   OP_FORMAT_UNKNOWN, /* OP_CALL */
   OP_FORMAT_UNKNOWN, /* OP_MCALL */
   OP_FORMAT_UNKNOWN, /* OP_SET */
   OP_FORMAT_UNKNOWN, /* OP_RET */
   OP_FORMAT_A_BX, /* OP_DEBUG */
};

kj_intern void prototype_release(struct prototype *proto, struct koji_allocator *allocator)
{
	if (--proto->references == 0) {
		assert(proto->references == 0);
		kj_free_type(proto->instructions, array_seq_capacity(proto->num_instructions), allocator);

		///* delete all child prototypes that reach reference to zero */
		for (int i = 0; i < proto->num_prototypes; ++i) {
			prototype_release(proto->prototypes[i], allocator);
		}
		kj_free_type(proto->prototypes, array_seq_capacity(proto->num_prototypes), allocator);

		///* destroy constant values */
		for (int i = 0; i < proto->num_constants; ++i) {
			constant_destroy(proto->constants[i], allocator);
		}
		kj_free_type(proto->constants, array_seq_capacity(proto->num_constants), allocator);
		kj_free_type(proto, 1, allocator);
	}
}

kj_intern void prototype_dump(struct prototype const* proto, int level)
{
	/* build a spacing string */
	int margin_length = level * 3;
	char *margin = alloca(margin_length + 1);
	for (int i = 0, n = margin_length; i < n; ++i)
		margin[i] = ' ';
	margin[margin_length] = '\0';

	printf("%sprototype \"%s\"\n#%sinstructions %d, #constants %d, #locals %d, #prototypes %d\n",
		margin, proto->name,
		margin, proto->num_instructions, proto->num_constants, proto->num_locals, 0);

	for (int i = 0; i < proto->num_instructions; ++i) {
		instruction_t instr = proto->instructions[i];
		enum opcode op = decode_op(instr);
		int regA = decode_A(instr);
		int regB = decode_B(instr);
		int regC = decode_C(instr);
		int regBx = decode_Bx(instr);

		printf("%d) %s", i + 1, OP_STRINGS[op]);

		/* tabbing */
		for (int j = 0, n = 10 - strlen(OP_STRINGS[op]); j < n; ++j) printf(" ");

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

		/* registers B and C might have constants, add a comment to the instruction to show the  constant value */
		if (constant_reg < 0) {
			printf("\t; ");
			value_t constant = proto->constants[-constant_reg - 1];
			if (value_is_number(constant)) {
				printf("%f", constant.number);
			}
			else if (value_is_object(constant)) {
				struct string *string = (struct string*)value_get_object(constant);
				//assert(string->object.class == string_class); (void)string_class;
				printf("\"%s\"", string->chars);
			}
		}

		if (offset != 0xffffffff) {
			printf("\t; to %d", offset + i + 2);
		}

		printf("\n");
	}
}
