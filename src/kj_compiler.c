/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_compiler.h"
#include "kj_bytecode.h"
#include "kj_lexer.h"
#include "kj_error.h"
#include "kj_support.h"
#include <string.h>

/*
 * #todo
 */
typedef int location_t;

/*
 * A local variable is simply a named and reserved stack register offset.
 */
typedef struct {
  uint       identifier_offset;
  location_t location;
} local_t;

/*
 * A label is a dynamic array of the indices of the instructions that branch to it.
 */
typedef array_type(uint) label_t;

/*
 * #todo
 */
typedef struct compiler {
   lexer_t             lexer;
   allocator_t        *allocator;
   linear_allocator_t *temp_allocator;
   char               *scope_identifiers;
   uint                scope_identifiers_size;
   location_t          temporary;
   label_t             label_true;
   label_t             label_false;
   prototype_t        *proto;
} compiler_t;

/*
 * #todo
 */
typedef enum {
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
 * #todo
 */
static const char *EXPR_TYPE_TO_STRING[] = { "nil", "bool", "number", "string", "local",
                                                     "bool", "bool", "bool" };
/*
 * The value of a string expression, the string [data] and its length [len].
 */
struct expr_string {
   char *chars;
   uint  length;
};

/*
 * #todo
 */
struct expr_comparison {
   location_t lhs;
   location_t rhs;
};

/*
 * Union of valid expression values data.
 */
union expr_value {
   bool                   boolean;
   koji_number_t            number;
   struct expr_string     string;
   location_t             location;
   struct expr_comparison comparison;
};

/*
 * #todo
 */
typedef struct {
   expr_type_t      type;
   union expr_value val;
   bool             positive;
} expr_t;

/*
 * #todo
 */
typedef enum {
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
 * #todo
 */
static const char *BINOP_TO_STR[] = { "<invalid>", "&&", " || ", " == ", " != ", "<", "<=", ">",
                                      ">=", "+", "-", "*",  "/",  "%", "&", "|", "^", "<<", ">>" };
/*
 * A state structure used to contain information about expression parsing and compilation such as
 * the desired target register, whether the expression should be negated and the indices of new jump
 * instructions in compiler state global true/false labels.
 */
typedef struct {
  location_t target;
  uint       true_jumps_begin;
  uint       false_jumps_begin;
  bool       negated;
} expr_state_t;

/*------------------------------------------------------------------------------------------------*/
/* expression                                                                                     */
/*------------------------------------------------------------------------------------------------*/
/* Returns a nil expr. */
static expr_t expr_nil(void)
{
   return (expr_t) { EXPR_NIL, .positive = true };
}

/* Makes and returns a number expr of specified [value]. */
static expr_t expr_boolean(bool value) {
  return (expr_t) { EXPR_BOOL, .val.boolean = value, .positive = true };
}

/* Makes and returns a boolean expr of specified [value]. */
static expr_t expr_number(koji_number_t value) {
  return (expr_t) { EXPR_NUMBER, .val.number = value, .positive = true };
}

/* Makes and returns a location expr of specified [value]. */
static expr_t expr_location(location_t value) {
  return (expr_t) { EXPR_LOCATION, .val.location = value, .positive = true };
}

/*
 * Makes and returns a string expr of specified [length] allocating in the compiler temp allocator. 
 */
static expr_t expr_new_string(compiler_t *c, uint length)
{
   return (expr_t) { EXPR_STRING, .val.string.length = length, .val.string.chars =
                     linear_allocator_alloc(&c->temp_allocator, c->allocator, length + 1, 1),
                     .positive = true };
}

/*
 * Makes and returns a comparison expr of specified [type between] [lhs_location] and [rhs_location]
 * against test value [test_value].
 */
static expr_t expr_comparison(expr_type_t type, bool test_value, int lhs_location,
                              int rhs_location)
{
   return (expr_t) { type, .val.comparison.lhs = lhs_location, .val.comparison.rhs = rhs_location,
                     .positive = test_value };
}


/*
 * Returns whether an expression of specified [expr] can be *statically* converted to a bool.
 */
static bool expr_is_statically_bool_convertible(expr_type_t type) {
  return type <= EXPR_STRING;
}

/*------------------------------------------------------------------------------------------------*/
/* parsing helper functions                                                                       */
/*------------------------------------------------------------------------------------------------*/
/*
 * Formats and reports a syntax error (unexpected <token>) at specified source location.
 */
static void syntax_error_at(compiler_t *c, source_location_t sourceloc)
{
   error(c->lexer.issue_handler, sourceloc, "unexpected %s.", lexer_lookahead_to_string(&c->lexer));
}

/*
 * Calls [syntax_error_at] passing current lexer source location.
 */
static void syntax_error(compiler_t *c)
{
   syntax_error_at(c, c->lexer.source_location);
}

/*
 * Returns whether the current lookahead is [tok].
 */
static bool peek(compiler_t *c, token_t tok)
{
   return c->lexer.lookahead == tok;
}

/*
 * Tells the compiler lexer to scan the next lookahead.
 */
 static token_t lex(compiler_t *c)
 {
    return lexer_scan(&c->lexer);
 }

/*
 * Scans next token if lookahead is @tok. Returns whether a new token was
 * scanned.
 */
static bool accept(compiler_t *c, token_t tok)
{
   if (peek(c, tok)) {
      lex(c);
      return true;
   }
   return false;
}

/*
 * Reports a compilation error if lookhead differs from @tok.
 */
static void check(compiler_t *c, token_t tok)
{
   if (!peek(c, tok)) {
      char token_string_buffer[64];
      error(c->lexer.issue_handler, c->lexer.source_location, "missing %s before '%s'.",
            lexer_token_to_string(tok, token_string_buffer, 64),
            lexer_lookahead_to_string(&c->lexer));
   }
}

/*
 * Checks that lookahead is @tok then scans next token.
 */
static void expect(compiler_t *c, token_t tok)
{
   check(c, tok);
   lex(c);
}

/*
 * Returns an "end of statement" token is found (newline, ';', '}' or end-of-stream) and "eats" it.
 */
static bool accept_end_of_stmt(compiler_t *c)
{
   if (accept(c, ';')) return true;
   if (c->lexer.lookahead == '}' || c->lexer.lookahead == tok_eos) return true;
   if (c->lexer.newline) {
      c->lexer.newline = false;
      return true;
   }
   return false;
}

/*
 * Expects an end of statement.
 */
static void expect_end_of_stmt(compiler_t *c)
{
   if (!accept_end_of_stmt(c)) syntax_error(c);
}

/*------------------------------------------------------------------------------------------------*/
/* compilation helper functions                                                                   */
/*------------------------------------------------------------------------------------------------*/

/*
 * Converts token [tok] to the corresponding binary operator.
 */
static binop_t token_to_binop(token_t tok) {
  switch (tok) {
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
 * If expression [e] is a register of location equal to current free register, it bumps up the free
 * register counter. It returns the old temporary register regardless whether if the current
 * temporary register was bumped up. After using the new temporary you must restore the temporary
 * register [c->temporary] to the value returned by this function.
 */
static int use_temporary(compiler_t *c, expr_t const *e) {
  int old_temporary = c->temporary;
  if ((e->type == EXPR_LOCATION /* || e->type == KC_EXPR_TYPE_ACCESSOR */) &&
      e->val.location == c->temporary) {
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
   char *dest_id = array_push_seq_n(&c->scope_identifiers, &c->scope_identifiers_size, c->allocator,
                                 char, id_len);
   memcpy(dest_id, id, id_len);
   return (uint) (dest_id - c->scope_identifiers); /* compute and return the offset of the newly
                                                      pushed identifier */
}

/*
 * Fetches or defines if not found a real constant [k] and returns its index.
 */
static int fetch_constant_number(compiler_t *c, koji_number_t k) {
  //value_t value = (value_t){0};
  //value.type = KJ_TYPE_REAL;
  //value.real = k;

  //for (uint i = 0; i < c->proto->num_constants; ++i) {
  //   if (memcmp(c->proto->constants + i, &k, sizeof(value_t)) == 0) return i;
  //}

  ///* constant not found, add it */
  //int index = c->proto->num_constants;
  //*seqarray_push(&c->proto->constants, &c->proto->num_constants, c->allocator,
  //   value_t) = k;

  //return index;
   return 0;
}

/*
 * Fetches or defines if not found a string constant [k] and returns its index.
 */
static int fetch_constant_string(compiler_t *c, const char *k) {
  //for (uint i = 0; i < c->proto->num_constants; ++i) {
  //  value_t *constant = c->proto->constants + i;
  //  if (constant->type == KJ_TYPE_STRING &&
  //      strcmp(k, ((string_t *)constant->object)->data) == 0)
  //    return i;
  //}

  ///* constant not found, add it */
  //int index = c->proto->num_constants;
  //value_t *constant = seqarray_push(
  //    &c->proto->constants, &c->proto->num_constants, c->allocator, value_t);
  //*constant = (value_t){0};

  //constant->type = KJ_TYPE_STRING;

  ///* create the string object */
  //uint str_length = (uint)strlen(k);
  //string_t *string = kj_malloc(sizeof(string_t) + str_length + 1, c->allocator);
  //constant->object = string;

  ///* setup the string object so that it always holds a reference (it is never
  //* destroyed)
  //* and the actual string buffer is right after the string object in memory
  //* (same allocation) */
  //string->references = 1;
  //string->length = str_length;
  //string->data = (char *)string + sizeof(string_t);
  //memcpy(string->data, k, str_length + 1);

  //return index;
   return 0;
}

/* Pushes instruction @i to current prototype instructions. */
static void emit(compiler_t *c, instruction_t i)
{
   opcode_t const op = decode_op(i);
   if (opcode_has_target(op)) {
      c->proto->num_registers = max_u(c->proto->num_registers, decode_A(i) + 1);
   }
   *array_push_seq(&c->proto->instructions, &c->proto->num_instructions, c->allocator,
                   instruction_t) = i;
}

/*
 * If expression [e] is not of type [EXPR_LOCATION] this function emits a sequence of instructions
 * so that the value or result of [e] is written to location [target_hint]. If [e] is already of
 * type [EXPR_LOCATION] this function does nothing. In either case, a location expression is
 * returned with its location value set to the local that will contain the compiled expression
 * value.
 */
static expr_t compile_expr_to_location(compiler_t *c, expr_t e, int target_hint)
{
   uint constant_index;
   int location;

   switch (e.type) {
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
         constant_index = fetch_constant_string(c, e.val.string.chars);
         goto make_constant;

      make_constant:
         location = -(int)constant_index - 1;
         if (constant_index <= MAX_REG_VALUE) {
            /* constant is small enough to be used as direct index */
            return expr_location(location);
         } else {
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
 * Helper function for parse_binary_expression_rhs() that actually compiles the binary operation
 * between @lhs and @rhs. This function also checks whether the operation can be optimized to a
 * constant if possible before falling back to emitting the actual instructions.
 */
static expr_t compile_binary_expression(compiler_t *c, expr_state_t const *es,
                                        source_location_t op_source_loc, binop_t op, expr_t lhs,
                                        expr_t rhs)
{
   #define DEFAULT_ARITH_INVALID_OPS_CHECKS()\
         if (lhs.type <= EXPR_BOOL   || rhs.type <= EXPR_BOOL) goto error;                         \
         if (lhs.type == EXPR_STRING || lhs.type == EXPR_STRING) goto error;                       \

   #define DEFAULT_ARITH_BINOP(opchar)                                                             \
         DEFAULT_ARITH_INVALID_OPS_CHECKS();                                                       \
         if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER) {                                 \
            return expr_number(lhs.val.number + rhs.val.number);                                   \
         }                                                                                         

   /* make a binary operator between our lhs and the rhs; */
   switch (op) {
      case BINOP_ADD:
         /* string concatenation */
         if (lhs.type == EXPR_STRING && rhs.type == EXPR_STRING) {
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
                  rhs.type == EXPR_STRING && lhs.type == EXPR_LOCATION) {
            break;
         }
         DEFAULT_ARITH_BINOP(+);
         break;

      case BINOP_MUL:
         /* string multiplication by a number, concatenate the string n times with itself */
         if (lhs.type == EXPR_STRING && rhs.type == EXPR_NUMBER) {
            const uint tot_length = lhs.val.string.length * (uint)rhs.val.number;
            expr_t e = expr_new_string(c, tot_length + 1);
            for (uint offset = 0; offset < tot_length; offset += lhs.val.string.length) {
               memcpy(e.val.string.chars, lhs.val.string.chars + offset, lhs.val.string.length);
            }
            e.val.string.chars[tot_length] = '\0';
            return e;
         }
         /* same reasons as for the BINOP_ADD case */
         else if (lhs.type == EXPR_STRING && rhs.type == EXPR_LOCATION ||
            rhs.type == EXPR_STRING && lhs.type == EXPR_LOCATION) {
            break;
         }
         DEFAULT_ARITH_BINOP(*);
         break;

      case BINOP_SUB: DEFAULT_ARITH_BINOP(-); break;
      case BINOP_DIV: DEFAULT_ARITH_BINOP(/); break;

      case BINOP_MOD:
         DEFAULT_ARITH_INVALID_OPS_CHECKS();
         if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER) {
            return expr_number((koji_number_t)((int64_t)lhs.val.number % (int64_t)rhs.val.number));
         }
         break;

#if 0
      /*
       * lhs is a register and we assume that the compiler has called "prepare_logical_operator_lhs"
       * before calling this hence the TESTSET instruction has already been emitted.
       */
      case BINOP_LOGICAL_AND:
         return (expr_is_statically_bool_convertible(lhs.type) && !expr_to_bool(lhs)) ?
                 expr_boolean(false) : rhs;

      case BINOP_LOGICAL_OR:
         return (expr_is_statically_bool_convertible(lhs.type) && expr_to_bool(lhs)) ? 
                 expr_boolean(true) : rhs;

      case BINOP_EQ:
      case BINOP_NEQ:
      {
         const bool invert = (op == BINOP_NEQ);
         if (lhs.type == EXPR_NIL || rhs.type == EXPR_NIL)
            return expr_boolean(((lhs.type == EXPR_NIL) == (rhs.type == EXPR_NIL)) ^ invert);
         else if (expr_is_constant(lhs.type) && expr_is_constant(rhs.type)) {
            if ((lhs.type == EXPR_BOOL) != (rhs.type == EXPR_BOOL))
               goto error;
            else if (lhs.type == EXPR_BOOL)
               lhs = expr_boolean(lhs.val.boolean == rhs.val.boolean) ^ invert);
            else if (lhs.type == EXPR_NUMBER || rhs.type == EXPR_NUMBER)
               lhs = expr_boolean((expr_to_real(lhs) == expr_to_real(rhs)) ^ invert);
            lhs = expr_boolean((lhs.integer == rhs.integer) ^ invert);
            return lhs;
         }
         break;
      }

      case KC_BINOP_LT:
      case KC_BINOP_GTE:
      {
         kj_bool invert = binop == KC_BINOP_GTE;
         if (lhs.type == EXPR_NIL) {
            lhs = expr_boolean((rhs.type == EXPR_NIL) == invert);
            return lhs;
         }

         if (rhs.type == EXPR_NIL) {
            lhs = expr_boolean((lhs.type == EXPR_NIL) != invert);
            return lhs;
         }

         if (expr_is_constant(lhs.type) && expr_is_constant(rhs.type)) {
            if ((lhs.type == EXPR_BOOL) != (rhs.type == EXPR_BOOL))
               goto error;
            if (lhs.type == EXPR_BOOL)
               lhs = expr_boolean((lhs.integer < rhs.integer) ^ invert);
            if (lhs.type == EXPR_REAL || rhs.type == EXPR_REAL)
               lhs = expr_boolean((expr_to_real(lhs) < expr_to_real(rhs)) ^ invert);
            lhs = expr_boolean((lhs.integer < rhs.integer) ^ invert);
            return lhs;
         }
         break;
      }

      case KC_BINOP_LTE:
      case KC_BINOP_GT:
      {
         kj_bool invert = binop == KC_BINOP_GT;
         if (lhs.type == EXPR_NIL) {
            lhs = expr_boolean((rhs.type == EXPR_NIL) == invert);
            return lhs;
         }

         if (rhs.type == EXPR_NIL) {
            lhs = expr_boolean((lhs.type == EXPR_NIL) != invert);
            return lhs;
         }

         if (expr_is_constant(lhs.type) && expr_is_constant(rhs.type)) {
            if ((lhs.type == EXPR_BOOL) != (rhs.type == EXPR_BOOL))
               goto error;
            if (lhs.type == EXPR_BOOL)
               lhs = expr_boolean((lhs.integer <= rhs.integer) ^ invert);
            if (lhs.type == EXPR_REAL || rhs.type == EXPR_REAL)
               lhs = expr_boolean((expr_to_real(lhs) <= expr_to_real(rhs)) ^ invert);
            lhs = expr_boolean((lhs.integer <= rhs.integer) ^ invert);
            return lhs;
         }
         break;
      }
#endif

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
   if (op >= BINOP_EQ && op <= BINOP_GTE) {
      /* maps binary operators to comparison expression types */
      static const expr_type_t COMPARISON_BINOP_TO_EXPR_TYPE[] = {
         /* eq       neq      lt       lte       gt        gte */
            EXPR_EQ, EXPR_EQ, EXPR_LT, EXPR_LTE, EXPR_LTE, EXPR_LT
      };

      /* maps binary operators to comparison expression testing values */
      static const bool COMPARISON_BINOP_TO_TEST_VALUE[] = {
         /* eq       neq      lt       lte       gt        gte */
            true,    false,   true,    true,    false,     false
      };

      expr_type_t etype    = COMPARISON_BINOP_TO_EXPR_TYPE[op - BINOP_EQ];
      bool        positive = COMPARISON_BINOP_TO_TEST_VALUE[op - BINOP_EQ];

      lhs = expr_comparison(etype, positive, lhs.val.location, rhs.val.location);
   }
   else {
      /* the binary operation is not a comparison but an arithmetic operation, emit the approriate
       * instruction */
      emit(c, encode_ABC(op - BINOP_ADD + OP_ADD, es->target, lhs.val.location, lhs.val.location));
      lhs = expr_location(es->target);
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

/*------------------------------------------------------------------------------------------------*/
/* parsing functions                                                                              */
/*------------------------------------------------------------------------------------------------*/

static expr_t parse_primary_expression(compiler_t *c, expr_state_t *es)
{
   source_location_t source_loc = c->lexer.source_location;
   expr_t expr;
   
   switch (c->lexer.lookahead)
   {
      /* literals */
      case kw_nil: lex(c); expr = expr_nil(); break;
      case kw_true: lex(c); expr = expr_boolean(true); break;
      case kw_false: lex(c); expr = expr_boolean(false); break;
      case tok_number: lex(c); expr = expr_number(c->lexer.token_number); break;
      default: syntax_error(c, source_loc); return expr_nil();
   }
   
   return expr;
}

static expr_t parse_binary_expression_rhs(compiler_t *c, expr_state_t const *es, expr_t lhs,
                                          int precedence)
{
   for (;;) {
      /* what's the lookahead operator? */
      binop_t binop = token_to_binop(c->lexer.lookahead);

      /* and what is it's precedence? */
      int tok_precedence = BINOP_PRECEDENCES[binop];

      /* if the next operator precedence is lower than expression precedence (or next token is not
       * an operator) then we're done.
       */
      if (tok_precedence < precedence) return lhs;

      /* remember operator source location as the expression location for error reporting */
      source_location_t source_loc = c->lexer.source_location;

      /* todo explain this */
      //compile_logical_operation(c, es, binop, lhs);

      lex(c); /* eat the operator token */

      /* if lhs uses the current free register, create a new state copy using the next register */
      location_t old_temporary = use_temporary(c, &lhs);
      expr_state_t es_rhs = *es;
      es_rhs.target = c->temporary;

      /* compile the right-hand-side of the binary expression */
      expr_t rhs = parse_primary_expression(c, &es_rhs);

      /* look at the new operator precedence */
      int next_binop_precedence = BINOP_PRECEDENCES[token_to_binop(c->lexer.lookahead)];

      /* if next operator precedence is higher than current expression, then call recursively this
       * function to give higher priority to the next binary operation (pass our rhs as their lhs)
       */
      if (next_binop_precedence > tok_precedence) {
         /* the target and whether the expression is currently negated are the same for the rhs, but
          * use reset the jump instruction lists as rhs is a subexpression on its own */
         es_rhs.target = es->target;
         es_rhs.negated = es->negated;
         es_rhs.true_jumps_begin = c->label_true.size;
         es_rhs.false_jumps_begin = c->label_false.size;

         /* parse the expression rhs using higher precedence than operator precedence */
         rhs = parse_binary_expression_rhs(c, &es_rhs, rhs, tok_precedence + 1);
      }

      /* subexpr has been evaluated, restore the free register to the one before compiling rhs */
      c->temporary = old_temporary;

      /* compile the binary operation */
      lhs = compile_binary_expression(c, es, source_loc, binop, lhs, rhs);
   }
}

static expr_t parse_expression(compiler_t *c, expr_state_t const *es)
{
   expr_state_t my_es = *es;

   /* store the source location for error reporting */
   source_location_t source_loc = c->lexer.source_location;
   
   /* parse expression lhs */
   expr_t lhs = parse_primary_expression(c, &my_es);

   /* if peek '=' parse the assignment `lhs = rhs` */
   #if 0
   if (accept(c, '=')) {
      
      switch (lhs.type) {
         case EXPR_LOCATION:
            /* check the location is a valid assignable, i.e. neither a constant nor a temporary */
            if (location_is_constant(lhs.value.location) ||
                location_is_temporary(c, lhs.value.location)) {
               goto error_lhs_not_assignable;
            }


         default:
            break;
      }
   }
   #endif
   
   return parse_binary_expression_rhs(c, &my_es, lhs, BINOP_PRECEDENCES[BINOP_INVALID]);

error_lhs_not_assignable:
   error(c->lexer.issue_handler, source_loc, "lhs of assignment is not an assignable expression."); 
   return expr_nil();
}

static expr_t parse_expression_to_location()
{
}

/*
 * Parses a variable declaration in the form `var <id> [= <expr>], ..., <idn> [= <exprn>]`.
 */
static void parse_variable_decl(compiler_t *c)
{
   expect(c, kw_var);

   /* parse multiple variable declarations */
   do {
      /* read the variable identifier and push it to the scope identifier list */
      check(c, tok_identifier);
      uint identifier_offset = push_scope_identifier(c, c->lexer.token_string,
                                                     c->lexer.token_string_length);
      lex(c);
      
      /* optionally, parse the initialization expression */
      if (accept(c, '=')) {
         //expr_t expr = parse_expression(c);
      }
   } while (accept(c, ','));
}

/*
 * Parses a single statement (e.g. a function definition, a local variable, an expression, etc.)
 */
static void parse_statement(compiler_t *c)
{
   switch (c->lexer.lookahead)
   {
      case kw_var: /* variable declaration */
         parse_variable_decl(c);
         expect_end_of_stmt(c);
         break;

      default: /* expression */
      {
         expr_state_t es = { c->temporary };
         parse_expression(c, &es);
         expect_end_of_stmt(c);
         break;
      }
   }
}

/*
 * Parses the body of a function, i.e. the statements contained in the function definition.
 */
static void parse_statements(compiler_t *c)
{
   while (!peek(c, '}') && !peek(c, tok_eos)) {
      parse_statement(c);
   }
}

/*
 * Parses the content of a script source file. This is nothing more but the body of the main
 * prototype followed by an end of stream.
 */
static void parse_module(compiler_t *c)
{
      parse_statements(c);
      expect(c, tok_eos);
}

kj_intern prototype_t *compile(compile_info_t const *info)
{
   compiler_t compiler = { 0 };

   /* create the source-file main prototype we will compile into */
   prototype_t *main_proto = kj_alloc(prototype_t, 1, info->allocator);

   /* define an issue handler from provided issue reporter */
   issue_handler_t issue_handler = { info->issue_reporter_fn, info->issue_reporter_data };

   /* redirect the error handler jump buffer here so that we can cleanup the state. */
   if (setjmp(issue_handler.error_jmpbuf)) goto error;

   /* initialize the lexer */
   lexer_init((lexer_info_t) {
      .allocator = info->allocator,
      .issue_handler = &issue_handler,
      .filename = info->source_name,
      .stream_fn = info->stream_fn,
      .stream_data = info->stream_data
   }, &compiler.lexer);

   /* finish setting up compiler state */
   compiler.allocator = info->allocator;
   compiler.temp_allocator = linear_allocator_create(info->allocator,
                                                     LINEAR_ALLOCATOR_PAGE_MIN_SIZE);

   /* kick off compilation! */
   parse_module(&compiler);

   goto cleanup;

error:
   kj_free(main_proto, info->allocator); /* #fixme */
   main_proto = NULL;

cleanup:
   kj_free(compiler.scope_identifiers, info->allocator);
   linear_allocator_destroy(compiler.temp_allocator, info->allocator);
   lexer_deinit(&compiler.lexer);
   return main_proto;
}
