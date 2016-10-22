/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano[gmail].com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_compiler.h"
#include "kj_bytecode.h"
#include "kj_lexer.h"
#include "kj_error.h"
#include "kj_support.h"
#include "kj_value.h"
#include <string.h>
#include <malloc.h>

/*
 * #documentation
 */
typedef int location_t;

/*
 * A local variable is simply a named and reserved stack register offset.
 */
typedef struct
{
   uint       identifier_offset;
   location_t location;
} local_t;

/*
 * A label is a dynamic array of the indices of the instructions that branch to it.
 */
typedef struct
{
   /* The array of branching instruction indexes in current prototype instruction array that branch
      to this label. */
   uint * instructions;

   /* The number of branching instructions branching to this label. */
   uint   num_instructions;

   /* The capacity of the [instructions] array (in number of elements). */
   uint   capacity;
} label_t;

/*
 * #documentation
 */
typedef struct compiler
{
   lexer_t              lexer;
   allocator_t         *allocator;
   linear_allocator_t  *temp_allocator;
   char*scope_identifiers;
   uint                 scope_identifiers_size;
   local_t *            locals;
   uint                 num_locals;
   location_t           temporary;
   label_t              label_true;
   label_t              label_false;
   prototype_t        * proto;
   klass_t            * class_string;
} compiler_t;

/*
 * #documentation
 */
typedef enum
{
   EXPR_NIL,      /* a nil expression */
   EXPR_BOOL,     /* a boolean expression */
   EXPR_NUMBER,   /* a numerical expression */
   EXPR_STRING,   /* a string expression */
   EXPR_LOCATION, /* a location expression */
   EXPR_EQ,       /* a equals logical expression */
   EXPR_LT,       /* a less-than logical expression */
   EXPR_LTE,      /* a less-than-equal logical expression */
} expr_type_t;

/*
 * #documentation
 */
static const char *EXPR_TYPE_TO_STRING[] = { "nil", "bool", "number", "string", "local", "bool",
                                             "bool", "bool" };
/*
 * The value of a string expression, the string [data] and its length [len].
 */
struct expr_string
{
   char *chars;
   uint  length;
};

/*
 * #documentation
 */
struct expr_comparison
{
   location_t lhs;
   location_t rhs;
};

/*
 * Union of valid expression values data.
 */
union expr_value
{
   bool                   boolean;
   kj_number_t          number;
   struct expr_string     string;
   location_t             location;
   struct expr_comparison comparison;
};

/*
 * #documentation
 */
typedef struct
{
   expr_type_t      type;
   union expr_value val;
   bool             positive;
} expr_t;

/*
 * #documentation
 */
typedef enum
{
   BINOP_INVALID,
   BINOP_MUL,
   BINOP_DIV,
   BINOP_MOD,
   BINOP_ADD,
   BINOP_SUB,
   BINOP_BIT_LSH, /* bitwise left shift */
   BINOP_BIT_RSH, /* bitwise right shift */
   BINOP_BIT_AND, /* bitwise and */
   BINOP_BIT_OR,  /* bitwise or */
   BINOP_BIT_XOR, /* bitwise xor */
   BINOP_LT,
   BINOP_LTE,
   BINOP_GT,
   BINOP_GTE,
   BINOP_EQ,
   BINOP_NEQ,
   BINOP_LOGICAL_AND,
   BINOP_LOGICAL_OR,
} binop_t;

/* binary operator precedences (the lower the number the higher the precedence) */
static const int BINOP_PRECEDENCES[] = {
   /* invalid   *   /   %   +  -  << >> &  ^  5  <  <= >  >= == != && || */
   -1,          10, 10, 10, 9, 9, 8, 8, 7, 6, 5, 4, 4, 4, 4, 3, 3, 2, 1
};

/*
 * #documentation
 */
static const char *BINOP_TO_STR[] = { "<invalid>", "*",  "/", "%", "+", "-", "<<",">>", "&", "|",
   "^", "<", "<=", ">", ">=", "==", "!=", "&&", "||" };

/*
 * #documentation
 */
static const opcode_t BINOP_TO_OPCODE[] = { 0, OP_MUL, OP_DIV, OP_MOD, OP_ADD, OP_SUB };

/*
 * A state structure used to contain information about expression parsing and compilation such as
 * the desired target register, whether the expression should be negated and the indices of new jump
 * instructions in compiler state global true/false labels.
 */
typedef struct
{
   uint true_branches_begin;
   uint false_branches_begin;
   bool negated;
   uint temporary;
} expr_state_t;

/*------------------------------------------------------------------------------------------------*/
/* location                                                                                       */
/*------------------------------------------------------------------------------------------------*/
/*
 * Returns whether [l] is a constant location.
 */
static bool location_is_constant(location_t l)
{
   return l < 0;
}

/*
 * Returns whether [l] is a temporary location, i.e. neither a constant nor a local.
 */
static bool location_is_temporary(compiler_t* c, location_t l)
{
   return l >= (location_t)c->num_locals;
}

/*------------------------------------------------------------------------------------------------*/
/* expression                                                                                     */
/*------------------------------------------------------------------------------------------------*/
/* Returns a nil expr. */
static expr_t expr_nil(void)
{
   return (expr_t) { EXPR_NIL, .positive = true };
}

/* Makes and returns a number expr of specified [value]. */
static expr_t expr_boolean(bool value)
{
   return (expr_t) { EXPR_BOOL, .val.boolean = value, .positive = true };
}

/* Makes and returns a boolean expr of specified [value]. */
static expr_t expr_number(kj_number_t value)
{
   return (expr_t) { EXPR_NUMBER, .val.number = value, .positive = true };
}

/* Makes and returns a location expr of specified [value]. */
static expr_t expr_location(location_t value)
{
   return (expr_t) { EXPR_LOCATION, .val.location = value, .positive = true };
}

/*
 * Makes and returns a string expr of specified [length] allocating in the compiler temp allocator.
 */
static expr_t expr_new_string(compiler_t* c, uint length)
{
   return (expr_t)
   {
      EXPR_STRING, .val.string.length = length, .val.string.chars =
         linear_allocator_alloc(&c->temp_allocator, c->allocator, length + 1, 1),
         .positive = true
   };
}

/*
 * Makes and returns a comparison expr of specified [type between] [lhs_location] and [rhs_location]
 * against test value [test_value].
 */
static expr_t expr_comparison(expr_type_t type, bool test_value, int lhs_location,
   int rhs_location)
{
   return (expr_t)
   {
      type, .val.comparison.lhs = lhs_location, .val.comparison.rhs = rhs_location,
         .positive = test_value
   };
}

/*
 * Returns whether expression is a constant (i.e. nil, boolean, number or string).
 */
static bool expr_is_constant(expr_type_t type)
{
   return type <= EXPR_STRING;
}

/*
 * Converts [expr] to a boolean. 'expr_is_constant(expr)' must return true.
 */
static bool expr_to_bool(expr_t expr)
{
   switch (expr.type)
   {
      case EXPR_NIL:
         return 0;

      case EXPR_BOOL:
      case EXPR_NUMBER:
         return expr.val.number != 0;

      default:
         assert(0);
         return 0;
   }
}

/*
 * Returns whether an expression of specified [type] is a comparison.
 */
static bool expr_is_comparison(expr_type_t type) { return type >= EXPR_EQ; }

/*
 * Applies a (logical) negation to the expression. It returns the negated result.
 */
static expr_t expr_negate(expr_t e)
{
   switch (e.type)
   {
      case EXPR_NIL:
         return expr_boolean(true);

      case EXPR_BOOL:
         return expr_boolean(!e.val.boolean);

      case EXPR_STRING:
         return expr_boolean(false);

      case EXPR_NUMBER:
         return expr_boolean(!expr_to_bool(e));

      default:
         e.positive = !e.positive;
         return e;
   }
}

/*------------------------------------------------------------------------------------------------*/
/* parsing helper functions                                                                       */
/*------------------------------------------------------------------------------------------------*/
/*
 * Formats and reports a syntax error (unexpected <token>) at specified source location.
 */
static void syntax_error_at(compiler_t* c, source_location_t sourceloc)
{
   error(c->lexer.issue_handler, sourceloc, "unexpected '%s'.",
      lexer_lookahead_to_string(&c->lexer));
}

/*
 * Calls [syntax_error_at] passing current lexer source location.
 */
static void syntax_error(compiler_t* c)
{
   syntax_error_at(c, c->lexer.source_location);
}

/*
 * Returns whether the current lookahead is [tok].
 */
static bool peek(compiler_t* c, token_t tok)
{
   return c->lexer.lookahead == tok;
}

/*
 * Tells the compiler lexer to scan the next lookahead.
 */
static token_t lex(compiler_t* c)
{
   return lexer_scan(&c->lexer);
}

/*
 * Scans next token if lookahead is [tok]. Returns whether a new token was
 * scanned.
 */
static bool accept(compiler_t* c, token_t tok)
{
   if (peek(c, tok))
   {
      lex(c);
      return true;
   }
   return false;
}

// Reports a compilation error if lookahead differs from [tok].
static void check(compiler_t* c, token_t tok)
{
   if (!peek(c, tok))
   {
      char token_string_buffer[64];
      error(c->lexer.issue_handler, c->lexer.source_location, "missing %s before '%s'.",
         lexer_token_to_string(tok, token_string_buffer, 64),
         lexer_lookahead_to_string(&c->lexer));
   }
}

/*
 * Checks that lookahead is [tok] then scans next token.
 */
static void expect(compiler_t* c, token_t tok)
{
   check(c, tok);
   lex(c);
}

/*
 * Returns an "end of statement" token is found (newline, ';', '}' or end-of-stream) and "eats" it.
 */
static bool accept_end_of_stmt(compiler_t* c)
{
   if (accept(c, ';')) return true;
   if (c->lexer.lookahead == '}' || c->lexer.lookahead == tok_eos) return true;
   if (c->lexer.newline)
   {
      c->lexer.newline = false;
      return true;
   }
   return false;
}

/*
 * Expects an end of statement.
 */
static void expect_end_of_stmt(compiler_t* c)
{
   if (!accept_end_of_stmt(c)) syntax_error(c);
}

/*------------------------------------------------------------------------------------------------*/
/* compilation helper functions                                                                   */
/*------------------------------------------------------------------------------------------------*/

/*
 * Writes the offset to jump instructions contained in [label] starting from
 * [begin] to target
 * instruction index target_index.
 */
static void label_bind_to(compiler_t *c, label_t *label, uint begin, int target_index)
{
  for (uint i = begin, size = label->num_instructions; i < size; ++i)
  {
    uint jump_instr_index = label->instructions[i];
    replace_Bx(c->proto->instructions + jump_instr_index, target_index - jump_instr_index - 1);
  }
  label->num_instructions= begin;
}

/*
 * Binds jump instructions in [label] starting from [begin] to the next instruction that will be
 * emitted to current prototype.
 */
kj_intern void label_bind_here(compiler_t *c, label_t *label, uint begin)
{
  label_bind_to(c, label, begin, c->proto->num_instructions);
}

/*
 * Converts token [tok] to the corresponding binary operator.
 */
static binop_t token_to_binop(token_t tok)
{
   switch (tok)
   {
      case '&&': return BINOP_LOGICAL_AND;
      case '||': return BINOP_LOGICAL_OR;
      case '==': return BINOP_EQ;
      case '!=': return BINOP_NEQ;
      case '<':  return BINOP_LT;
      case '<=': return BINOP_LTE;
      case '>':  return BINOP_GT;
      case '>=': return BINOP_GTE;
      case '+':  return BINOP_ADD;
      case '-':  return BINOP_SUB;
      case '*':  return BINOP_MUL;
      case '/':  return BINOP_DIV;
      case '%':  return BINOP_MOD;
      case '&':  return BINOP_BIT_AND;
      case '|':  return BINOP_BIT_OR;
      case '^':  return BINOP_BIT_XOR;
      case '<<': return BINOP_BIT_LSH;
      case '>>': return BINOP_BIT_RSH;
      default:   return BINOP_INVALID;
   }
}

/*
 * #documentation
 */
expr_state_t make_expr_state(compiler_t* c, bool negated)
{
   return (expr_state_t)
   {
      c->label_true.num_instructions, /* true_branches_begin  */
      c->label_false.num_instructions, /* false_branches_begin  */
      negated, /* negated  */
      c->temporary, /* temporary */
   };
}

/*
 * If expression [e] is a register of location equal to current free register, it bumps up the free
 * register counter. It returns the old temporary register regardless whether if the current
 * temporary register was bumped up. After using the new temporary you must restore the temporary
 * register [c->temporary] to the value returned by this function.
 */
static int use_temporary(compiler_t* c, expr_t const *e)
{
   int old_temporary = c->temporary;
   if ((e->type == EXPR_LOCATION /* || e->type == KC_EXPR_TYPE_ACCESSOR */) &&
      e->val.location == c->temporary)
   {
      ++c->temporary;
   }
   return old_temporary;
}

/*
 * Pushes an identifier string pointed by [id] of specified length [id_len] into the current scope
 * identifier list withing compiler [c], then returns the offset within [c->scope_identifiers] of
 * pushed identifier string.
 */
static uint push_scope_identifier(compiler_t * c, const char *id, uint id_len)
{
   ++id_len; /* include null byte */
   char *dest_id = array_push_seq_n(&c->scope_identifiers, &c->scope_identifiers_size, c->allocator, char, id_len);
   memcpy(dest_id, id, id_len);
   return (uint)(dest_id - c->scope_identifiers); /* compute and return the offset of the newly
                                                      pushed identifier */
}

/*
 * Defines a new local variable in current prototype with an identifier starting at
 * [identifier_offset] preiously pushed through [push_scope_identifier()].
 */
static void push_scope_local(compiler_t* c, uint identifier_offset)
{
   local_t *local = array_push_seq(&c->locals, &c->num_locals, c->allocator, local_t);
   local->identifier_offset = identifier_offset;
   local->location = c->temporary;
   ++c->temporary;
}

/*
 * Finds a local with matching [identifier] starting from the current scope and iterating over its
 * parents until. If none could be found it returns NULL. #TODO add upvalues.
 */
static local_t * fetch_scope_local(compiler_t* c, const char *identifier)
{
   for (int i = c->num_locals - 1; i >= 0; --i)
   {
      const char *local_identifier = c->scope_identifiers + c->locals[i].identifier_offset;
      if (strcmp(local_identifier, identifier) == 0)
      {
         return c->locals + i;
      }
   }
   return NULL;
}

/*
 * Fetches or defines if not found a real constant [k] and returns its index.
 */
static int fetch_constant_number(compiler_t* c, kj_number_t k)
{
   value_t value = value_number(k);

   for (uint i = 0; i < c->proto->num_constants; ++i)
   {
      /* constant already existent, return it */
      if (c->proto->constants[i].bits == value.bits) return i;
   }

   /* constant not found, add it */
   int index = c->proto->num_constants;
   *array_push_seq(&c->proto->constants, &c->proto->num_constants, c->allocator,
      value_t) = value;

   return index;
}

/*
 * Fetches or defines if not found a string constant [str] and returns its index.
 */
static int fetch_constant_string(compiler_t* c, const char *str, uint str_len)
{
   for (uint i = 0; i < c->proto->num_constants; ++i)
   {
      value_t *constant = c->proto->constants + i;
      object_t *object;

      /* is i-th constant a string and do the strings match? if so, no need to add a new constant */
      if (value_is_object(*constant)
         && (object = value_get_object(*constant))->klass == c->class_string
         && ((string_t const*)object)->size == str_len
         && memcmp(((string_t const*)object)->chars, str, str_len) == 0)
      {
         return i;
      }
   }

   /* constant not found, push the new constant to the array */
   value_t *constant = array_push_seq(&c->proto->constants, &c->proto->num_constants, c->allocator, value_t);

   /* create a new string */
   string_t *string = string_new(c->allocator, c->class_string, str_len);
   memcpy(string->chars, str, str_len);
   string->chars[str_len] = '\0';

   *constant = value_object(string);

   /* return the index of the pushed constant */
   return constant - c->proto->constants;
}

/* Pushes instruction [i] to current prototype instructions. */
static void emit(compiler_t* c, instruction_t i)
{
   opcode_t const op = decode_op(i);

   /* if instruction has target, update the current prototype total number of used registers */
   if (opcode_has_target(op))
   {
      c->proto->num_registers = max_u(c->proto->num_registers, decode_A(i) + 1);
   }

   *array_push_seq(&c->proto->instructions, &c->proto->num_instructions, c->allocator,
      instruction_t) = i;
}

/*
 * If expression [e] is not of type `EXPR_LOCATION` this function emits a sequence of instructions
 * so that the value or result of [e] is written to location [target_hint]. If [e] is already of
 * type `EXPR_LOCATION` this function does nothing. In either case, a location expression is
 * returned with its location value set to the local that will contain the compiled expression
 * value.
 */
static expr_t compile_expr_to_location(compiler_t* c, expr_t e, int target_hint)
{
   uint constant_index;
   int location;

   switch (e.type)
   {
      case EXPR_NIL:
         emit(c, encode_ABx(OP_LOADNIL, target_hint, target_hint));
         return expr_location(target_hint);

      case EXPR_BOOL:
         emit(c, encode_ABC(OP_LOADBOOL, target_hint, e.val.boolean, 0));
         return expr_location(target_hint);

      case EXPR_NUMBER:
         constant_index = fetch_constant_number(c, e.val.number);
         goto make_constant;

      case EXPR_STRING:
         constant_index = fetch_constant_string(c, e.val.string.chars, e.val.string.length);
         goto make_constant;

      make_constant:
         location = -(int)constant_index - 1;
         if (constant_index <= MAX_REG_VALUE)
         {
            /* constant is small enough to be used as direct index */
            return expr_location(location);
         } else
         {
            /* constant too large, load it into a temporary register */
            emit(c, encode_ABx(OP_MOV, target_hint, location));
            return expr_location(target_hint);
         }

      case EXPR_LOCATION:
         if (e.positive) return e;
         emit(c, encode_ABx(OP_NEG, target_hint, e.val.location));
         return expr_location(target_hint);

         /*  case KC_EXPR_TYPE_ACCESSOR:
              emit(c, encode_ABC(OP_GET, target_hint, e.lhs, e.rhs));
              if (!e.positive) emit(c, encode_ABx(OP_NEG, target_hint, target_hint));
              return expr_location(target_hint);*/

      case EXPR_EQ:
      case EXPR_LT:
      case EXPR_LTE:
         /* compile the comparison expression to a sequence of instructions that write into
          * [target_hint] the value of the comparison */
         emit(c, encode_ABC(OP_EQ + e.type - EXPR_EQ, e.val.comparison.lhs, e.positive,
            e.val.comparison.rhs));
         emit(c, encode_ABx(OP_JUMP, 0, 1));
         emit(c, encode_ABC(OP_LOADBOOL, target_hint, false, 1));
         emit(c, encode_ABC(OP_LOADBOOL, target_hint, true, 0));
         return expr_location(target_hint);

      default:
         assert(!"Unreachable");
         return expr_nil();
   }
}

/*
 * Compiles the unary minus of expression [e] and returns the result.
 */
static expr_t compile_unary_minus(compiler_t* c, source_location_t sourceloc, expr_t e)
{
   switch (e.type)
   {
      case EXPR_NUMBER:
         return expr_number(-e.val.number);

      case EXPR_LOCATION:
         emit(c, encode_ABx(OP_UNM, c->temporary, e.val.location));
         return expr_location(c->temporary);

      default:
         error(c->lexer.issue_handler, sourceloc,
            "cannot apply operator unary minus to a value of type %s.", EXPR_TYPE_TO_STRING[e.type]);
   }
   return expr_nil();
}

// Helper function for parse_binary_expression_rhs() that actually compiles the binary operation
// between [lhs] and [rhs]. This function also checks whether the operation can be optimized to a
// constant if possible before falling back to emitting the actual instructions.
static expr_t compile_binary_expression(compiler_t* c, source_location_t op_source_loc, binop_t op, expr_t lhs, expr_t rhs)
{
   #define DEFAULT_ARITH_INVALID_OPS_CHECKS()\
         if (lhs.type <= EXPR_BOOL   || rhs.type <= EXPR_BOOL) goto error;                         \
         if (lhs.type == EXPR_STRING || lhs.type == EXPR_STRING) goto error;                       \

   #define DEFAULT_ARITH_BINOP(opchar)                                                             \
         DEFAULT_ARITH_INVALID_OPS_CHECKS();                                                       \
         if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER) {                                 \
            return expr_number(lhs.val.number opchar rhs.val.number);                              \
         }                                                                                         

   /* make a binary operator between our lhs and the rhs; */
   switch (op)
   {
      case BINOP_ADD:
         /* string concatenation */
         if (lhs.type == EXPR_STRING && rhs.type == EXPR_STRING)
         {
            expr_t e = expr_new_string(c, lhs.val.string.length + rhs.val.string.length);
            memcpy(e.val.string.chars, lhs.val.string.chars, lhs.val.string.length);
            memcpy(e.val.string.chars + lhs.val.string.length, rhs.val.string.chars,
               rhs.val.string.length + 1);
            return e;
         }
         /* skip the DEFAULT_ARITH_BINOP as it throws an error if any operand is a string, but no
          * error should be thrown if we're adding a string to a location as we don't know what type
          * the value at that location will have at runtime
          */
         else if (lhs.type == EXPR_STRING && rhs.type == EXPR_LOCATION ||
            rhs.type == EXPR_STRING && lhs.type == EXPR_LOCATION)
         {
            break;
         }
         DEFAULT_ARITH_BINOP(+);
         break;

      case BINOP_MUL:
         /* string multiplication by a number, concatenate the string n times with itself */
         if (lhs.type == EXPR_STRING && rhs.type == EXPR_NUMBER)
         {
            const uint tot_length = lhs.val.string.length * (uint)rhs.val.number;
            expr_t e = expr_new_string(c, tot_length + 1);
            for (uint offset = 0; offset < tot_length; offset += lhs.val.string.length)
            {
               memcpy(e.val.string.chars, lhs.val.string.chars + offset, lhs.val.string.length);
            }
            e.val.string.chars[tot_length] = '\0';
            return e;
         }
         /* same reasons as for the BINOP_ADD case */
         else if (lhs.type == EXPR_STRING && rhs.type == EXPR_LOCATION ||
            rhs.type == EXPR_STRING && lhs.type == EXPR_LOCATION)
         {
            break;
         }
         DEFAULT_ARITH_BINOP(*);
         break;

      case BINOP_SUB: DEFAULT_ARITH_BINOP(-); break;
      case BINOP_DIV: DEFAULT_ARITH_BINOP(/ ); break;

      case BINOP_MOD:
         DEFAULT_ARITH_INVALID_OPS_CHECKS();
         if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER)
         {
            return expr_number((kj_number_t)((int64_t)lhs.val.number % (int64_t)rhs.val.number));
         }
         break;

         /*
          * lhs is a register and we assume that the compiler has called "prepare_logical_operator_lhs"
          * before calling this hence the TESTSET instruction has already been emitted.
          */
      case BINOP_LOGICAL_AND:
         return (expr_is_constant(lhs.type) && !expr_to_bool(lhs)) ?
            expr_boolean(false) : rhs;

      case BINOP_LOGICAL_OR:
         return (expr_is_constant(lhs.type) && expr_to_bool(lhs)) ?
            expr_boolean(true) : rhs;


      case BINOP_EQ:
      case BINOP_NEQ:
      {
         const bool invert = (op == BINOP_NEQ);
         if (lhs.type == EXPR_NIL || rhs.type == EXPR_NIL)
            return expr_boolean(((lhs.type == EXPR_NIL) == (rhs.type == EXPR_NIL)) ^ invert);
         if (expr_is_constant(lhs.type) && expr_is_constant(rhs.type))
         {
            if (lhs.type == EXPR_BOOL && rhs.type == EXPR_BOOL)
               return expr_boolean((lhs.val.boolean == rhs.val.boolean) ^ invert);
            if (lhs.type == EXPR_STRING && rhs.type == EXPR_STRING)
               return expr_boolean(
               (lhs.val.string.length == rhs.val.string.length &&
                  memcmp(lhs.val.string.chars, rhs.val.string.chars, lhs.val.string.length) == 0)
                  ^ invert);
            if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER)
               return expr_boolean((lhs.val.number == rhs.val.number) ^ invert);
            goto error;
         }
      }

      case BINOP_LT:
      case BINOP_GTE:
      {
         bool invert = (op == BINOP_GTE);
         if (lhs.type == EXPR_NIL)
            return expr_boolean((rhs.type == EXPR_NIL) == invert);
         if (rhs.type == EXPR_NIL)
            return expr_boolean((lhs.type == EXPR_NIL) != invert);
         if (expr_is_constant(lhs.type) && expr_is_constant(rhs.type))
         {
            if (lhs.type == EXPR_BOOL && rhs.type == EXPR_BOOL)
               return expr_boolean((lhs.val.boolean < rhs.val.boolean) ^ invert);
            if (lhs.type == EXPR_STRING && rhs.type == EXPR_STRING)
            {
               bool lt = lhs.val.string.length < rhs.val.string.length ||
                  (lhs.val.string.length == rhs.val.string.length &&
                     memcmp(lhs.val.string.chars, rhs.val.string.chars, lhs.val.string.length) < 0);
               return expr_boolean(lt ^ invert);
            }
            if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER)
               return expr_boolean((lhs.val.number < rhs.val.number) ^ invert);
            goto error;
         }
         break;
      }

      case BINOP_LTE:
      case BINOP_GT:
      {
         bool invert = (op == BINOP_GT);
         if (lhs.type == EXPR_NIL)
            return expr_boolean((rhs.type == EXPR_NIL) == invert);
         if (rhs.type == EXPR_NIL)
            return expr_boolean((lhs.type == EXPR_NIL) != invert);
         if (expr_is_constant(lhs.type) && expr_is_constant(rhs.type))
         {
            if (lhs.type == EXPR_BOOL && rhs.type == EXPR_BOOL)
               return expr_boolean((lhs.val.boolean <= rhs.val.boolean) ^ invert);
            if (lhs.type == EXPR_STRING && rhs.type == EXPR_STRING)
            {
               bool lt = lhs.val.string.length <= rhs.val.string.length ||
                  (lhs.val.string.length == rhs.val.string.length &&
                     memcmp(lhs.val.string.chars, rhs.val.string.chars, lhs.val.string.length) <= 0);
               return expr_boolean(lt ^ invert);
            }
            if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER)
               return expr_boolean((lhs.val.number <= rhs.val.number) ^ invert);
            goto error;
         }
         break;
      }

      default: break;
   }

   /* if we get here, lhs or rhs is a register, the binary operation instruction must be omitted. */
   lhs = compile_expr_to_location(c, lhs, c->temporary);

   /* if lhs is using the current temporary (e.g., it's constant that has been moved to the free
    * temporary because its index is too large), mark the temporary as used and remember the old
    * temporary location to be restored later */
   location_t old_temporary = use_temporary(c, &lhs);

   /* compile the expression rhs to a register as well using a potentially new temporary */
   rhs = compile_expr_to_location(c, rhs, c->temporary);

   /* both lhs and rhs are now compiled to registers, restore the old temporary location */
   c->temporary = old_temporary;

   /* if the binary operation is a comparison generate and return a comparison expression */
   if (op >= BINOP_LT && op <= BINOP_NEQ)
   {
      /* maps binary operators to comparison expression types */
      static const expr_type_t COMPARISON_BINOP_TO_EXPR_TYPE[] = {
         /* lt       lte       gt        gte      eq        neq */
            EXPR_LT, EXPR_LTE, EXPR_LTE, EXPR_LT, EXPR_EQ, EXPR_EQ
      };

      /* maps binary operators to comparison expression testing values */
      static const bool COMPARISON_BINOP_TO_TEST_VALUE[] = {
         /* lt    lte   gt     gte    eq    neq */
            true, true, false, false, true, false
      };

      expr_type_t expr_type = COMPARISON_BINOP_TO_EXPR_TYPE[op - BINOP_LT];
      bool        positive = COMPARISON_BINOP_TO_TEST_VALUE[op - BINOP_LT];

      lhs = expr_comparison(expr_type, positive, lhs.val.location, rhs.val.location);
   } else
   {
      /* the binary operation is not a comparison but an arithmetic operation, emit the appropriate
       * instruction */
      emit(c, encode_ABC(BINOP_TO_OPCODE[op], c->temporary, lhs.val.location, rhs.val.location));
      lhs = expr_location(c->temporary);
   }

   return lhs;

   /* the binary operation between lhs and rhs is invalid */
error:
   error(c->lexer.issue_handler, op_source_loc, "cannot make binary operation '%s' between values"
      " of type '%s' and '%s'.", BINOP_TO_STR[op], EXPR_TYPE_TO_STRING[lhs.type],
      EXPR_TYPE_TO_STRING[rhs.type]);
   return expr_nil();

   #undef MAKEBINOP
}

/*
 * Computes and returns the offset specified from instruction index to the next instruction
 * that will be emitted in current prototype.
 */
static int offset_to_next_instruction(compiler_t* c, int from_instruction_index)
{
   return c->proto->num_instructions - from_instruction_index - 1;
}

/*
 * Compiles the lhs of a logical expression if [op] is such and [lhs] is a register or comparison.
 * In a nutshell, the purpose of this function is to patch the current early out branches to the
 * true or false label depending on [op], the truth value (positivity) of lhs and whether the whole
 * expression should be negated (as stated by [es]).
 * What this function does is: compiles the comparison if lhs is one, or emit the test/testset if
 * is a register if say op is an OR then it patches all existing branches to false in the compiled
 * expression represented by lhs to this point so that the future rhs will "try again" the OR. The
 * opposite holds for AND (jumps to true are patched to evaluate the future rhs because evaluating
 * only the lhs to true is not enough in an AND).
 */
static void compile_logical_operation(compiler_t* c, const expr_state_t* es, binop_t op, expr_t lhs)
{
   if ((lhs.type != EXPR_LOCATION && !expr_is_comparison(lhs.type))
      || (op != BINOP_LOGICAL_AND && op != BINOP_LOGICAL_OR))
      return;

   prototype_t *proto = c->proto;

   bool test_value = (op == BINOP_LOGICAL_OR) ^ es->negated;

   /* compile condition */
   switch (lhs.type)
   {
      case EXPR_LOCATION:
         if (!lhs.positive == es->negated)
         {
            emit(c, encode_ABC(OP_TESTSET, MAX_REG_VALUE, lhs.val.location, test_value));
         } else
         {
            assert(lhs.val.location >= 0); /* is non-constant */
            emit(c, encode_ABC(OP_TEST, lhs.val.location, !test_value, 0));
         }
         break;

      case EXPR_EQ: case EXPR_LT: case EXPR_LTE:
         emit(c, encode_ABC(OP_EQ + lhs.type - EXPR_EQ, lhs.val.comparison.lhs,
            lhs.val.comparison.rhs, (lhs.positive ^ es->negated) ^ !test_value));
         break;

      default: assert(false);
   }

   /* push jump instruction index to the appropriate label. */
   uint* offset = array_push(test_value ? &c->label_true : &c->label_false, c->allocator, uint);
   *offset = proto->num_instructions;
   emit(c, OP_JUMP);

   /* and */
   label_t *pjump_vector = &c->label_true;
   uint begin = es->true_branches_begin;

   if (test_value) /* or */
   {
      pjump_vector = &c->label_false;
      begin = es->false_branches_begin;
   }

   uint num_jumps;
   while ((num_jumps = pjump_vector->num_instructions) > begin)
   {
      /* get the index of the last jump instruction in the jump vector */
      uint index = pjump_vector->instructions[pjump_vector->num_instructions - 1];

      /*
       * if instruction before the current jump instruction is a TESTSET, turn it to a simple TEST
       * instruction as its "set" is wasted since we need to test more locations before we can
       * finally set the target (example "c = a && b", if a is true, don't c just yet, we need to
       * test b first)
       */
      if (index > 0 && decode_op(proto->instructions[index - 1]) == OP_TESTSET)
      {
         instruction_t instr = proto->instructions[index - 1];

         int tested_reg = decode_B(instr);
         bool flag = (bool)decode_C(instr);

         proto->instructions[index - 1] = encode_ABx(OP_TEST, tested_reg, flag);
      }

      /*
       * affix the jump to this instruction as more testing is needed to determine if expression
       * is true or false.
       */
      replace_Bx(proto->instructions + index, offset_to_next_instruction(c, index));

      /* shrink the size of the true or false jump vector by one */
      pjump_vector->num_instructions = num_jumps - 1;
   }
}

/*
 * #documentation
 */
static void close_expression(compiler_t* c, expr_state_t *es, expr_t expr, location_t target)
{
   prototype_t* proto = c->proto;
   uint true_label_instr_count = es->true_branches_begin;
   uint false_label_instr_count = es->false_branches_begin;

   /* declare some bookkeeping flags */
   bool value_is_comparison = expr_is_comparison(expr.type);
   uint rhs_move_jump_index = 0;
   bool set_value_to_false = false;

   if (value_is_comparison)
   {
      emit(c, encode_ABC(OP_EQ + expr.type - EXPR_EQ, expr.val.comparison.lhs, expr.val.comparison.rhs, expr.positive));
      *array_push(&c->label_true, c->allocator, uint) = proto->num_instructions;
      emit(c, encode_ABx(OP_JUMP, 0, 0));
      set_value_to_false = true;
   }
   else
   {
      /*
       * compiled expression instance that will hold final instruction location and the instructions
       * that ultimately write to that location
       */
      location_t location = compile_expr_to_location(c, expr, target).val.location;

      if (location != target)
      {
         /*
          * if from location is a temporary location (i.e. not a local variable) and we know from [from]
          * that one or more instructions are setting to this location, we can simply replace the A
          * operand of all those instructions to [to] and optimize out the 'move' instruction.
          */
         if (location >= c->temporary)
         {
            /* first check whether last instruction targets old location, if so update it with the new
               desired location */
            instruction_t *instr = &c->proto->instructions[c->proto->num_instructions - 1];
            if (opcode_has_target(decode_op(*instr)) && decode_A(*instr) == location)
            {
               /* target matches result expression target register? if so, replace it with [to] */
               replace_A(instr, target);
            }
         }
         else
         {
            /* we could not optimize out the move instruction, emit it. */
            emit(c, encode_ABx(OP_MOV, target, location));
         }
      }

      if (c->label_true.num_instructions <= true_label_instr_count && c->label_false.num_instructions <= false_label_instr_count)
      {
         goto done;
      }

      rhs_move_jump_index = proto->num_instructions;
      emit(c, encode_ABx(OP_JUMP, 0, 0));
   }

   /*
    * iterate over instructions that branch to false and if any is not a testset instruction, it
    * means that we need to emit a loadbool instruction to set the result to false, so for now just
    * remember this by flagging set_value_to_false to true.
    * Also update the target register of the TESTSET instruction to the actual target register.
    */
   for (uint i = false_label_instr_count; i < c->label_false.num_instructions; ++i)
   {
      uint index = c->label_false.instructions[i];
      if (index > 0)
      {
         instruction_t *instr = &proto->instructions[index - 1];
         if (decode_op(*instr) == OP_TESTSET)
         {
            replace_A(instr, target);
         } else
         {
            set_value_to_false = true;
            replace_Bx(proto->instructions + index, offset_to_next_instruction(c, index));
         }
      }
   }

   /*
    * if we need to set the result to false, emit the loadbool instruction (to false) now and
    * remember its index so that we can eventually patch it later
    */
   uint load_false_instruction_index = 0;
   if (set_value_to_false)
   {
      load_false_instruction_index = proto->num_instructions;
      emit(c, encode_ABC(OP_LOADBOOL, target, false, 0));
   }

   /*
    * analogous to the false case, iterate over the list of instructions branching to true, flag
    * set_value_to_true if instruction is not a testset, as we'll need to emit a loadbool to true
    * instruction in such case.
    * Also patch all jumps to this point as the next instruction emitted could be the loadbool to
    * true.
    */
   bool set_value_to_true = false;
   for (uint i = true_label_instr_count, size = c->label_true.num_instructions; i < size; ++i)
   {
      uint index = c->label_true.instructions[i];
      if (index > 0)
      {
         instruction_t *instr = &proto->instructions[index - 1];
         if (decode_op(*instr) == OP_TESTSET)
         {
            replace_A(instr, target);
         } else
         {
            set_value_to_true = true;
            replace_Bx(proto->instructions + index, offset_to_next_instruction(c, index));
         }
      }
   }

   /* emit the loadbool instruction to *true* if we need to */
   if (set_value_to_true)
   {
      emit(c, encode_ABC(OP_LOADBOOL, target, true, 0));
   }

   /*
    * if we emitted a loadbool to *false* instruction before, we'll need to patch the jump offset to
    * the current position (after the eventual loadbool to *true* has been emitted)
    */
   if (set_value_to_false)
   {
      replace_C(proto->instructions + load_false_instruction_index,
         offset_to_next_instruction(c, load_false_instruction_index));
   }

   /*
    * If the final subexpression was a register, check if we have added any loadb instruction.
    * If so, set the right jump offset to this location, otherwise pop the last instruction which is
    * the "jump" after the "mov" or "neg" to skip the loadbool instructions.
    */
   if (!value_is_comparison)
   {
      if (!set_value_to_true && !set_value_to_false)
      {
         --proto->num_instructions;
      } else
      {
         replace_Bx(proto->instructions + rhs_move_jump_index,
            offset_to_next_instruction(c, rhs_move_jump_index));
      }
   }

   /*
    * finally set the jump offset of all remaining TESTSET instructions generated by the expression
    * to true...
    */
   for (uint i = true_label_instr_count, size = c->label_true.num_instructions; i < size; ++i)
   {
      uint index = c->label_true.instructions[i];
      if (index > 0 && decode_op(proto->instructions[index - 1]) == OP_TESTSET)
      {
         replace_Bx(proto->instructions + index, offset_to_next_instruction(c, index));
      }
   }

   /* ...and to false to the next instruction. */
   for (uint i = false_label_instr_count; i < c->label_false.num_instructions; ++i)
   {
      uint index = c->label_false.instructions[i];
      if (index > 0 && decode_op(proto->instructions[index - 1]) == OP_TESTSET)
      {
         replace_Bx(proto->instructions + index, offset_to_next_instruction(c, index));
      }
   }

done:
   /* restore the compilation state */
   c->label_true.num_instructions = es->true_branches_begin;
   c->label_false.num_instructions = es->false_branches_begin;
   c->temporary = es->temporary;
}

/*------------------------------------------------------------------------------------------------*/
/* parsing functions                                                                              */
/*------------------------------------------------------------------------------------------------*/
static expr_t parse_expression(compiler_t *, expr_state_t *);
static void parse_expression_to(compiler_t* c, location_t target);

/*
 * #documentation
 */
static expr_t parse_local_ref_or_function_call(compiler_t* c, expr_state_t *es)
{
   uint id_len = c->lexer.lookahead_string_length;

   /* temporary remember the identifier as we need to carry on lexing to know whether it's a local
      variable reference or a function call */
   char *id = alloca(id_len + 1);
   memcpy(id, c->lexer.lookahead_string, id_len + 1);
   lex(c);

   /* identifier refers to local variable? */
   local_t *local = fetch_scope_local(c, id);
   if (local)
   {
      return expr_location(local->location);
   }

   assert(!"todo");
   (void)es;
   return expr_nil();
}

/*
 * Parses and returns a subexpression as in "(a + 2)".
 */
static expr_t parse_subexpression(compiler_t* c, expr_state_t *es)
{
   lex(c); /* eat the '(' */

   /* prepare a sub expression state identical to the incoming state but with updated number of
      temporaries */
   expr_state_t sub_es = *es;
   sub_es.temporary = c->temporary;

   /* parse the subexpression with local expression state */
   expr_t expr = parse_expression(c, &sub_es);

   expect(c, ')');

   /* if what follows the subexpression is anything but a logical operator (and/or) then it
      the expression must be closed */
   switch (c->lexer.lookahead)
   {
      case '+': case '-': case '*': case '/': case '(': case '&': case '|': case '[':
         close_expression(c, &sub_es, expr, sub_es.temporary);
         expr = expr_location(sub_es.temporary);
   }

   return expr;
}

/*
 * Parses and returns a primary expression, i.e. constants, unary expressions, subexpressions,
 * function calls.
 */
static expr_t parse_primary_expression(compiler_t* c, expr_state_t *es)
{
   source_location_t source_loc = c->lexer.source_location;
   expr_t expr = expr_nil();

   switch (c->lexer.lookahead)
   {
      /* literals */
      case kw_nil: lex(c); expr = expr_nil(); break;
      case kw_true: lex(c); expr = expr_boolean(true); break;
      case kw_false: lex(c); expr = expr_boolean(false); break;
      case tok_number: lex(c); expr = expr_number(c->lexer.lookahead_number); break;
      case tok_string:
         expr = expr_new_string(c, c->lexer.lookahead_string_length);
         memcpy(expr.val.string.chars, c->lexer.lookahead_string, c->lexer.lookahead_string_length);
         lex(c);
         break;

      case '(': /* subexpression */
         expr = parse_subexpression(c, es);
         break;

      case '!': /* negation */
         lex(c);
         es->negated = !es->negated;
         expr = expr_negate(parse_primary_expression(c, es));
         es->negated = !es->negated;
         break;

      case '-': /* unary minus */
         lex(c);
         expr = compile_unary_minus(c, source_loc, parse_primary_expression(c, es));
         break;

      case tok_identifier:
         expr = parse_local_ref_or_function_call(c, es);
         break;

      default: syntax_error_at(c, source_loc); return expr_nil();
   }

   return expr;
}

/*
 * Parses and compiles the potential right hand side of a binary expression if binary operators
 * are found. Note that the right hand side of a binary expression may well be a chain of additional
 * binary expressions. This function parses the chain of binary expressions taking care of operator
 * precedence. While the precedence of the next binary operator is equal or less than the current
 * one the function keeps parsing the expression (primary) terms in a loop. As soon as a higher
 * precedence operator is found, the function recursively calls itself so that the next operator to
 * give priority to that operator.
 */
static expr_t parse_binary_expression_rhs(compiler_t* c, expr_state_t *es, expr_t lhs,
   int precedence)
{
   for (;;)
   {
      /* what's the lookahead operator? */
      binop_t binop = token_to_binop(c->lexer.lookahead);

      /* and what is it's precedence? */
      int tok_precedence = BINOP_PRECEDENCES[binop];

      /* if the next operator precedence is lower than expression precedence (or next token is not
         an operator) then we're done. */
      if (tok_precedence < precedence) return lhs;

      /* remember operator source location as the expression location for error reporting */
      source_location_t source_loc = c->lexer.source_location;

      /* todo explain this */
      compile_logical_operation(c, es, binop, lhs);

      lex(c); /* eat the operator token */

      /* if lhs uses the current free register, create a new state copy using the next register */
      location_t old_temporary = use_temporary(c, &lhs);
      expr_state_t es_rhs = *es;

      /* compile the right-hand-side of the binary expression */
      expr_t rhs = parse_primary_expression(c, &es_rhs);

      /* look at the new operator precedence */
      int next_binop_precedence = BINOP_PRECEDENCES[token_to_binop(c->lexer.lookahead)];

      /* if next operator precedence is higher than current expression, then call recursively this
         function to give higher priority to the next binary operation (pass our rhs as their lhs) */
      if (next_binop_precedence > tok_precedence)
      {
         /* the target and whether the expression is currently negated are the same for the rhs, but
            reset the jump instruction lists as rhs is a subexpression on its own */
         es_rhs.negated = es->negated;
         es_rhs.true_branches_begin = c->label_true.num_instructions;
         es_rhs.false_branches_begin = c->label_false.num_instructions;

         /* parse the expression rhs using higher precedence than operator precedence */
         rhs = parse_binary_expression_rhs(c, &es_rhs, rhs, tok_precedence + 1);
      }

      /* sub-expr has been evaluated, restore the free register to the one before compiling rhs */
      c->temporary = old_temporary;

      /* compile the binary operation */
      lhs = compile_binary_expression(c, source_loc, binop, lhs, rhs);
   }
}

/*
 * Parses and returns a subexpression, a sequence of primary expressions separated by binary
 * operators. This function takes an explicit, existing expression state so that it can be called
 * to parse sub expressions of an existing expression.
 * After calling this function, you might want to move the result to a different
 * location by calling [close_expression()].
 */
static expr_t parse_expression(compiler_t* c, expr_state_t *es)
{
   expr_state_t my_es = *es;

   /* store the source location for error reporting */
   source_location_t source_loc = c->lexer.source_location;

   /* parse expression lhs */
   expr_t lhs = parse_primary_expression(c, &my_es);

   /* if peek '=' parse the assignment `lhs = rhs` */
   if (accept(c, '='))
   {

      if (!lhs.positive) goto error_lhs_not_assignable;

      switch (lhs.type)
      {
         case EXPR_LOCATION:
            /* check the location is a valid assignable, i.e. neither a constant nor a temporary */
            if (location_is_constant(lhs.val.location) || location_is_temporary(c, lhs.val.location))
            {
               goto error_lhs_not_assignable;
            }
            parse_expression_to(c, lhs.val.location);
            return lhs;

         default:
            break;
      }
   }

   return parse_binary_expression_rhs(c, &my_es, lhs, 0);

error_lhs_not_assignable:
   error(c->lexer.issue_handler, source_loc, "lhs of assignment is not an assignable expression.");
   return expr_nil();
}

/*
 * Parses and compiles a full expression. A full expression is a subexpression with a "cleared"
 * expression state (e.g. no existing branches, current temporary as target and positive flag set
 * to true). After the subexpression is compiled, it resolves any existing branch to true or false
 * by emitting the appropriate test/testset/loadbool/jump/etc instructions.
 * The expression returned by this function is guaranteed to lie in some location, whether a local
 * or a constant.
 */
static void parse_expression_to(compiler_t* c, location_t target)
{
   /* save the current number of branching instructions so that we can the state as we found it
      upon return. Also backup the first free temporary to be restored before returning. */
   expr_state_t es = {
      c->label_true.num_instructions, /* true_branches_begin */
      c->label_false.num_instructions, /* false_branches_begin */
      false, /* negated */
      c->temporary, /* temporary */
   };

   /* parse the subexpression with our blank state. The parsed expression might have generated some
      branching instructions to true/false. */
   expr_t expr = parse_expression(c, &es);

   /* immediately close the expression, i.e. make sure it is compiled to the 'target' location and
      all open branches are closed */
   close_expression(c, &es, expr, target);
}

/*
 * Parses a variable declaration in the form `var <id> [= <expr>], ..., <idn> [= <exprn>]`.
 */
static void parse_variable_decl(compiler_t* c)
{
   expect(c, kw_var);

   /* parse multiple variable declarations */
   do
   {
      /* read the variable identifier and push it to the scope identifier list */
      check(c, tok_identifier);
      uint identifier_offset = push_scope_identifier(c, c->lexer.lookahead_string, c->lexer.lookahead_string_length);
      lex(c);

      /* optionally, parse the initialization expression */
      if (accept(c, '='))
      {
         /* parse the expression and make sure it lies in the current temporary */
         parse_expression_to(c, c->temporary);
      }
      else
      {
         /* no initialization expression provided for this variable, initialize it to nil */
         emit(c, encode_ABx(OP_LOADNIL, c->temporary, c->temporary));
      }

      /* define the local variable */
      push_scope_local(c, identifier_offset);

   } while (accept(c, ','));
}


/*
 * Parses and compiles an expression only focusing on emitting appropriate branching instructions to
 * the true or false branch. The expression parsed &compiled is tested against [test_value], i.e. if
 * the expression is [test_value] (true or false) then it branches to *true*, otherwise to false.
 */
static void parse_condition(compiler_t* c, bool test_value)
{
   prototype_t* proto = c->proto;
   expr_state_t es = make_expr_state(c, !test_value);

   /* parse the expression */
   expr_t expr = parse_expression(c, &es);

   /* soft-close the expression, only emit branching instructions. We don't care to set the final
      expression value to some register. Only jump to the two labels based on the expression result */
   if (expr_is_comparison(expr.type))
   {
      emit(c, encode_ABC(OP_EQ + expr.type - EXPR_EQ, expr.val.comparison.lhs, expr.val.comparison.rhs, expr.positive ^ !test_value));
   }
   else
   {
      expr = compile_expr_to_location(c, expr, c->temporary);
      emit(c, encode_ABx(OP_TEST, expr.val.location, test_value));
   }

   uint jump_to_true_index = proto->num_instructions;
   *array_push(&c->label_true, c->allocator, uint) = jump_to_true_index;
   emit(c, encode_ABx(OP_JUMP, 0, 0));

   c->temporary = es.temporary;
}

static void parse_block(compiler_t* c);

/*
 * Parses and compiles an if statement.
 */
static void parse_if_stmt(compiler_t* c)
{
   prototype_t* proto = c->proto;
   expect(c, kw_if);

   uint label_true_begin = c->label_true.num_instructions;
   uint label_false_begin = c->label_false.num_instructions;

   /* parse the condition to branch to 'true' if it's false. */
   expect(c, '(');
   parse_condition(c, false);
   expect(c, ')');

   /* bind the true branch (contained in the false label) and parse the true
    * branch block. */
   label_bind_here(c, &c->label_false, label_false_begin);
   parse_block(c);

   /* check if there's a else block ahead */
   if (accept(c, kw_else))
   {
      /* emit the jump from the true branch that will skip the else block */
      uint exit_jump_index = proto->num_instructions;
      emit(c, OP_JUMP);

      /* bind the label to "else branch" contained in the true label (remember,
       * we're compiling the condition to false, */
       /* so labels are s swapped). */
      label_bind_here(c, &c->label_true, label_true_begin);

      if (peek(c, kw_if))
         parse_if_stmt(c);
      else
         parse_block(c);

      /* patch the previous jump expression */
      replace_Bx(proto->instructions + exit_jump_index, offset_to_next_instruction(c, exit_jump_index));
   }
   else
   {
      /* just bind the exit branch */
      label_bind_here(c, &c->label_true, label_true_begin);
   }
}

/*
 * Parses a single statement (e.g. a function definition, a local variable, an expression, etc.)
 */
static void parse_statement(compiler_t* c)
{
   switch (c->lexer.lookahead)
   {
      case kw_var: /* variable declaration */
         parse_variable_decl(c);
         expect_end_of_stmt(c);
         break;

      case kw_if: /* if statement */
         parse_if_stmt(c);
         break;

      default: /* expression */
      {
         expr_state_t es = make_expr_state(c, false);
         parse_expression(c, &es);
         expect_end_of_stmt(c);
         break;
      }
   }
}

/*
 * Parses the body of a function, i.e. the statements contained in the function definition.
 */
static void parse_statements(compiler_t* c)
{
   while (!peek(c, '}') && !peek(c, tok_eos))
   {
      parse_statement(c);
   }
}

/*
 * Parses and compiles an entire "{ stmts... }" block.
 */
static void parse_block(compiler_t* c)
{
   expect(c, '{');
   parse_statements(c);
   expect(c, '}');
}

/* Parses the body of a prototype, i.e. its instructions. */
static void parse_prototype_body(compiler_t* c)
{
   parse_statements(c);

   /* emit a return nil instruction anyway */
   emit(c, encode_ABx(OP_RET, 0, 0));
}

/*
 * Parses the content of a script source file. This is nothing more but the body of the main
 * prototype followed by an end of stream.
 */
static void parse_module(compiler_t* c)
{
   parse_prototype_body(c);
   expect(c, tok_eos);
}

kj_intern prototype_t *compile(compile_info_t const *info)
{
   compiler_t compiler = { 0 };

   /* create the source-file main prototype we will compile into */
   prototype_t *main_proto = kj_alloc(prototype_t, 1, info->allocator);
   if (!main_proto)
   {
      return NULL;
   }

   *main_proto = (prototype_t) { 0 };
   main_proto->references = 1;
   main_proto->name = "[name]";

   /* define an issue handler from provided issue reporter */
   issue_handler_t issue_handler = { info->issue_reporter_fn, info->issue_reporter_data };

   /* redirect the error handler jump buffer here so that we can cleanup the state. */
   if (setjmp(issue_handler.error_jmpbuf)) goto error;

   /* initialize the lexer */
   lexer_info_t lexer_info = { 0 };
   lexer_info.allocator = info->allocator;
   lexer_info.issue_handler = &issue_handler;
   lexer_info.filename = info->source_name;
   lexer_info.stream_fn = info->stream_fn;
   lexer_info.stream_data = info->stream_data;

   lexer_init(&lexer_info, &compiler.lexer);

   /* finish setting up compiler state */
   compiler.allocator = info->allocator;
   compiler.temp_allocator = linear_allocator_create(info->allocator, LINEAR_ALLOCATOR_PAGE_MIN_SIZE);
   compiler.proto = main_proto;
   compiler.class_string = info->class_string;

   /* kick off compilation! */
   parse_module(&compiler);

   goto cleanup;

error:
   prototype_release(main_proto, info->allocator);
   main_proto = NULL;

cleanup:
   kj_free(compiler.scope_identifiers, info->allocator);
   linear_allocator_destroy(compiler.temp_allocator, info->allocator);
   lexer_deinit(&compiler.lexer);
   return main_proto;
}
