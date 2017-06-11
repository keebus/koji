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
#include "kvalue.h"

/*
 * Enumeration that lists all Virtual Machine opcodes
 * Note: If this list is modified, update opcode formats in 'bytecode.c' too.
 */
enum opcode {
  /* operations that write into R(A) */
   OP_LOADNIL,  /* loadnil A, Bx;   ; R(A), ..., R(Bx) = nil */
   OP_LOADBOOL, /* loadbool A, B, C ; R(A) = bool(B) then jump by C */
   OP_MOV,      /* mov A, Bx        ; R(A) = R(Bx) */
   OP_NEG,      /* neg A, Bx        ; R(A) = not R(Bx) */
   OP_UNM,      /* unm A, Bx        ; R(A) = -R(Bx) */
   OP_ADD,      /* add A, B, C      ; R(A) = R(B) + R(C) */
   OP_SUB,      /* sub A, B, C      ; R(A) = R(B) - R(C) */
   OP_MUL,      /* mul A, B, C      ; R(A) = R(B) * R(C) */
   OP_DIV,      /* div A, B, C      ; R(A) = R(B) / R(C) */
   OP_MOD,      /* mod A, B, C      ; R(A) = R(B) % R(C) */
   OP_POW,      /* pow A, B, C      ; R(A) = pow(R(B), R(C)) */
   OP_TESTSET,  /* testset A, B, C  ; if R(B) == (bool)C then
                                          R(A) = R(B) else jump 1 */
   OP_CLOSURE,  /* closure A, Bx    ; R(A) = closure for prototype Bx */
   OP_GETGLOB,  /* getglob A, Bx    ; get global val with key R(Bx) into R(A)*/
   OP_NEWTABLE, /* newtable A       ; creates a new table in R(A) */
   OP_GET,      /* get A, B, C      ; R(A) = R(B)[R(C)] */
   OP_THIS,     /* this A           ; R(A) = this */

   /* operations that do not write into R(A) */
   OP_TEST,     /* test A, Bx        ; if (bool)R(A) != (bool)B then jump 1 */
   OP_JUMP,     /* jump Bx           ; jump by Bx instructions */
   OP_EQ,       /* eq A, B, C        ; if (R(A) == R(B)) == (bool)C
                                          then nothing else jump 1 */
   OP_LT,       /* lt A, B, C        ; if (R(A) < R(B)) == (bool)C
                                          then nothing else jump 1 */
   OP_LTE,      /* lte A, B, C       ; if (R(A) <= R(B)) == (bool)C
                                          then nothing else jump 1 */
   OP_CALL,     /* call A, B, C      ; call object R(C) with B arguments
                                          starting at R(A) */
   OP_MCALL,    /* mcall A, B, C     ; call object R(A - 1) method with name
                                          R(B) with C arguments from R(A) on */
   OP_SETGLOB,  /* setglob A, Bx     ; set global val R(A) with key R(Bx) */
   OP_SET,      /* set A, B, C       ; R(A)[R(B)] = R(C) */
   OP_RET,      /* ret A, B          ; return values R(A), ..., R(B)*/
   OP_THROW,    /* throw A           ; throws an error with R(A) msg string */
   OP_DEBUG,    /* debug A, Bx       ; (temp) prints Bx registers from R(A) */
};

static const char *OP_STRINGS[] = {
    "loadnil",  "loadbool", "mov",   "neg",   "unm",     "add",     "sub",
    "mul",      "div",      "mod",   "pow",   "testset", "closure", "getglob",
    "newtable", "get",   "this",  "test",    "jump",    "eq",      "lt",
    "lte",      "call",  "mcall", "setglob", "set",     "ret",     "throw",
    "debug",
};

/* Type of a single instruction, always a 32bit long */
typedef uint32_t instr_t;

/* Maximum value a 8-bit register (A, B or C) can hold (positive or negative)*/
static const int32_t MAX_ABC_VALUE = 255;

/* Maximum value Bx can hold (positive or negative) */
static const int32_t MAX_BX_VALUE = 131071;

/*
 * Returns whether opcode @op involves writing into register A.
 */
static bool
op_has_target(enum opcode op)
{
   return op <= OP_THIS;
}

/*
 * Encodes an instruction with arguments A and Bx.
 */
static instr_t
encode_ABx(enum opcode op, int32_t A, int32_t Bx)
{
   assert(A >= 0);
   assert((Bx < 0 ? -Bx : Bx) <= MAX_BX_VALUE);
   return (Bx << 14) | (A & 0xff) << 6 | op;
}

/*
 * Encodes and returns an instruction with arguments A, B and C.
 */
static instr_t
encode_ABC(enum opcode op, int32_t A, int32_t B, int32_t C)
{
   assert(A >= 0);
   return (C << 23) | (B & 0x1ff) << 14 | (A & 0xff) << 6 | op;
}

/*
 * Decodes an instruction opcode.
 */
static enum opcode
decode_op(instr_t i)
{
   return i & 0x3f;
}

/*
 * Decodes an instruction argument A.
 */
static int32_t
decode_A(instr_t i)
{
   return (i >> 6) & 0xff;
}

/*
 * Decodes an instructions argument B.
 */
static int32_t
decode_B(instr_t i)
{
   return ((int32_t)i << 9) >> 23;
}

/*
 * Decodes an instruction argument C.
 */
static int32_t
decode_C(instr_t i)
{
   return (int32_t)i >> 23;
}

/*
 * Decodes an instruction argument Bx.
 */
static int32_t
decode_Bx(instr_t i)
{
   return (int32_t)i >> 14;
}

/*
 * Sets instruction argument A.
 */
static void
replace_A(instr_t *i, int32_t A)
{
   assert(A >= 0);
   *i = (*i & 0xFFFFC03F) | (A << 6);
}

/*
 * Sets instruction argument Bx.
 */
static void
replace_Bx(instr_t *i, int32_t Bx)
{
   *i = (*i & 0x3FFF) | (Bx << 14);
}

/*
 * Set instruction argument C.
 */
static void
replace_C(instr_t *i, int32_t C)
{
   *i = (*i & 0x7FFFFF) | (C << 23);
}

/* prototype */

/*
 */
struct prototype {
   int32_t refs;
   uint16_t size;
   uint16_t ninstrs;
   uint16_t nargs;
   uint16_t nregs;
   uint16_t nconsts;
   uint16_t nprotos;
   instr_t *instrs;
   struct prototype **protos;
   union value consts[];
};

/*
 */
kintern struct prototype *
prototype_new(int32_t nconsts, int32_t ninstrs, int32_t nprotos,
   struct koji_allocator *alloc);

/*
 */
kintern void
prototype_release(struct prototype *proto, struct koji_allocator *alloc);

/*
 * Dumps the compiled prototype bytecode to stdout for debugging showing the
 * bytecode disassembly, constants for [proto] as well inner prototypes.
 */
kintern void
prototype_dump(struct prototype const *proto, int32_t index, int32_t level);
