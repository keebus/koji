/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "kj_support.h"
#include <assert.h>

/* Enumeration that lists all Virtual Machine opcodes */
typedef enum {
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
} opcode_t;

static const char *OP_STRINGS[] = {
    "loadnil",  "loadb", "mov",  "neg",   "unm",     "add",     "sub",
    "mul",      "div",   "mod",  "pow",   "testset", "closure", "globals",
    "newtable", "get",   "this", "test",  "jump",    "eq",      "lt",
    "lte",      "scall", "call", "mcall", "set",     "ret",
};

/* Type of a single instruction, always a 32bit long */
typedef uint32_t instruction_t;

/* Maximum value a 8-bit register (A, B or C) can hold (positive or negative) */
static const uint MAX_REG_VALUE = 255;

/* Maximum value Bx can hold (positive or negative) */
static const int  MAX_BX_VALUE = 131071;

/*-----------------------------------------------------------------------------------------------*/
/* instruction manipulation functions                                                            */
/*-----------------------------------------------------------------------------------------------*/

/* Returns whether opcode @op involves writing into register A. */
static inline bool opcode_has_target(opcode_t op) { return op <= OP_THIS; }

/* Encodes an instruction with arguments A and Bx. */
static inline instruction_t encode_ABx(opcode_t op, int A, int Bx)
{
   assert(A >= 0);
   assert((Bx < 0 ? -Bx : Bx) <= MAX_BX_VALUE);
   return (Bx << 14) | (A & 0xff) << 6 | op;
}

/* Encodes and returns an instruction with arguments A, B and C. */
static inline instruction_t encode_ABC(opcode_t op, int A, int B, int C)
{
   assert(A >= 0);
   return (C << 23) | (B & 0x1ff) << 14 | (A & 0xff) << 6 | op;
}

/* Decodes an instruction opcode. */
static inline opcode_t decode_op(instruction_t i) { return i & 0x3f; }

/* Decodes an instruction argument A. */
static inline int decode_A(instruction_t i) { return (i >> 6) & 0xff; }

/* Decodes an instructions argument B. */
static inline int decode_B(instruction_t i) { return ((int)i << 9) >> 23; }

/* Decodes an instruction argument C. */
static inline int decode_C(instruction_t i) { return (int)i >> 23; }

/* Decodes an instruction argument Bx. */
static inline int decode_Bx(instruction_t i) { return (int)i >> 14; }

/* Sets instruction argument A. */
static inline void replace_A(instruction_t *i, int A)
{
   assert(A >= 0);
   *i = (*i & 0xFFFFC03F) | (A << 6);
}

/* Sets instruction argument Bx. */
static inline void replace_Bx(instruction_t *i, int Bx)
{
   *i = (*i & 0x3FFF) | (Bx << 14);
}

/* Set instruction argument C */
static inline void replace_C(instruction_t *i, int C)
{
   *i = (*i & 0x7FFFFF) | (C << 23);
}

typedef struct prototype {
   uint            temporaries;
   uint            num_locals;
   uint            num_registers;
   instruction_t * instructions;
   uint            num_instructions;
   union value   * constants;
   uint            num_constants;
} prototype_t;
