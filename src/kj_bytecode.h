/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "kj_support.h"

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

typedef struct prototype {
   uint temporaries;
} prototype_t;
