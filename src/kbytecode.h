/*
 * koji - opcodes and bytecode manipulation
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kplatform.h"

/*
 * Enumeration that lists all Virtual Machine opcodes
 * Note: If this list is modified, update opcode formats in 'bytecode.c' too.
 */
enum opcode {
  /* operations that write into R(A) */
   OP_LOADNIL,  /* loadnil A, Bx;    ; R(A), ..., R(Bx) = nil */
   OP_LOADBOOL, /* loadbool A, B, C  ; R(A) = kbool(B) then jump by C */
   OP_MOV,      /* mov A, Bx         ; R(A) = R(Bx) */
   OP_NEG,      /* neg A, Bx         ; R(A) = not R(Bx) */
   OP_UNM,      /* unm A, Bx         ; R(A) = -R(Bx) */
   OP_ADD,      /* add A, B, C       ; R(A) = R(B) + R(C) */
   OP_SUB,      /* sub A, B, C       ; R(A) = R(B) - R(C) */
   OP_MUL,      /* mul A, B, C       ; R(A) = R(B) * R(C) */
   OP_DIV,      /* div A, B, C       ; R(A) = R(B) / R(C) */
   OP_MOD,      /* mod A, B, C       ; R(A) = R(B) % R(C) */
   OP_POW,      /* pow A, B, C       ; R(A) = pow(R(B), R(C)) */
   OP_TESTSET,  /* testset A, B, C   ; if R(B) == (kbool)C then
                                          R(A) = R(B) else jump 1 */
   OP_CLOSURE,  /* closure A, Bx     ; R(A) = closure for prototype Bx */
   OP_GLOBALS,  /* globals A         ; get the global table into register A */
   OP_NEWTABLE, /* newtable A        ; creates a new table in R(A) */
   OP_GET,      /* get A, B, C       ; R(A) = R(B)[R(C)] */
   OP_THIS,     /* this A            ; R(A) = this */

   /* operations that do not write into R(A) */
   OP_TEST,     /* test A, Bx        ; if (kbool)R(A) != (kbool)B then jump 1 */
   OP_JUMP,     /* jump Bx           ; jump by Bx instructions */
   OP_EQ,       /* eq A, B, C        ; if (R(A) == R(B)) == (kbool)C
                                          then nothing else jump 1 */
   OP_LT,       /* lt A, B, C        ; if (R(A) < R(B)) == (kbool)C
                                          then nothing else jump 1 */
   OP_LTE,      /* lte A, B, C       ; if (R(A) <= R(B)) == (kbool)C
                                          then nothing else jump 1 */
   OP_SCALL,    /* scall A, B, C     ; call static function at K[B] with C
                                          arguments starting from R(A) */
   OP_CALL,     /* call A, B, C      ; call closure R(B) with C arguments
                                          starting at R(A) */
   OP_MCALL,    /* mcall A, B, C     ; call object R(A - 1) method with name
                                          R(B) with C arguments from R(A) on */
   OP_SET,      /* set A, B, C       ; R(A)[R(B)] = R(C) */
   OP_RET,      /* ret A, B          ; return values R(A), ..., R(B)*/

   OP_DEBUG,    /* debug A, Bx       ; (temp) prints Bx registers from R(A) */
};

static const char *OP_STRINGS[] = {
    "loadnil",  "loadb", "mov",  "neg",   "unm",     "add",     "sub",
    "mul",      "div",   "mod",  "pow",   "testset", "closure", "globals",
    "newtable", "get",   "this", "test",  "jump",    "eq",      "lt",
    "lte",      "scall", "call", "mcall", "set",     "ret",     "debug",
};

/* Type of a single instruction, always a 32bit long */
typedef u32 instr_t;

/* Maximum value a 8-bit register (A, B or C) can hold (positive or negative)*/
static const int MAX_ABC_VALUE = 255;

/* Maximum value Bx can hold (positive or negative) */
static const int MAX_BX_VALUE = 131071;

/*
 * Returns whether opcode @op involves writing into register A.
 */
kinline kbool
opcode_has_target(enum opcode op)
{
   return op <= OP_THIS;
}

/*
 * Encodes an instruction with arguments A and Bx.
 */
kinline instr_t
encode_ABx(enum opcode op, i32 A, i32 Bx)
{
   assert(A >= 0);
   assert((Bx < 0 ? -Bx : Bx) <= MAX_BX_VALUE);
   return (Bx << 14) | (A & 0xff) << 6 | op;
}

/*
 * Encodes and returns an instruction with arguments A, B and C.
 */
kinline instr_t
encode_ABC(enum opcode op, i32 A, i32 B, i32 C)
{
   assert(A >= 0);
   return (C << 23) | (B & 0x1ff) << 14 | (A & 0xff) << 6 | op;
}

/*
 * Decodes an instruction opcode.
 */
kinline enum opcode
decode_op(instr_t i)
{
   return i & 0x3f;
}

/*
 * Decodes an instruction argument A.
 */
kinline i32
decode_A(instr_t i)
{
   return (i >> 6) & 0xff;
}

/*
 * Decodes an instructions argument B.
 */
kinline i32
decode_B(instr_t i)
{
   return ((i32)i << 9) >> 23;
}

/*
 * Decodes an instruction argument C.
 */
kinline i32
decode_C(instr_t i)
{
   return (i32)i >> 23;
}

/*
 * Decodes an instruction argument Bx.
 */
kinline i32
decode_Bx(instr_t i)
{
   return (i32)i >> 14;
}

/*
 * Sets instruction argument A.
 */
kinline void
replace_A(instr_t *i, i32 A)
{
   assert(A >= 0);
   *i = (*i & 0xFFFFC03F) | (A << 6);
}

/*
 * Sets instruction argument Bx.
 */
kinline void
replace_Bx(instr_t *i, i32 Bx)
{
   *i = (*i & 0x3FFF) | (Bx << 14);
}

/*
 * Set instruction argument C.
 */
kinline void
replace_C(instr_t *i, i32 C)
{
   *i = (*i & 0x7FFFFF) | (C << 23);
}

/* prototype */

/*
 */
struct prototype {
   i32 refs;
   i32 ninstrs;
   u16 nargs;
   u16 nlocals;
   u16 nconsts;
   u16 nprotos;
   instr_t *instrs;
   union value *consts;
   struct prototype **protos;
   const char *name;
};

/*
 */
kintern void
prototype_release(struct prototype *proto, struct koji_allocator *alloc);

/*
 * Dumps the compiled prototype bytecode to stdout for debugging showing the
 * bytecode disassembly, constants for [proto] as well inner prototypes.
 */
kintern void
prototype_dump(struct prototype const *proto, i32 level);
