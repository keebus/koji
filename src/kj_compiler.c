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
} compiler_t;

/*
 * #todo
 */
typedef enum {
   EXPR_NIL,
   EXPR_BOOL,
   EXPR_NUMBER,
   EXPR_STRING,
   EXPR_LOCATION,
} expr_type_t;

/*
 * The value of a string expression, the string [data] and its length [len].
 */
struct expr_string {
   char *data;
   uint  length;
};

/*
 * Union of valid expression values data.
 */
union expr_value {
   bool               boolean;
   koji_number        number;
   struct expr_string string;
   location_t         location;
};

/*
 * #todo
 */
typedef struct {
   expr_type_t      type;
   union expr_value value;
   bool             negative;
} expr_t;

typedef enum {
   BINOP_INVALID,
   BINOP_LOGICAL_AND,
   BINOP_LOGICAL_OR,
   BINOP_EQ,
   BINOP_NEQ,
   BINOP_LT,
   BINOP_LTE,
   BINOP_GT,
   BINOP_GTE,
   BINOP_ADD,
   BINOP_SUB,
   BINOP_MUL,
   BINOP_DIV,
   BINOP_MOD,
   BINOP_BIT_AND, /* bitwise and */
   BINOP_BIT_OR,  /* bitwise or */
   BINOP_BIT_XOR, /* bitwise xor */
   BINOP_BIT_LSH, /* bitwise left shift */
   BINOP_BIT_RSH, /* bitwise right shift */
} binop_t;

/* binary operator precedences (the lower the number the higher the precedence) */
static const int BINOP_PRECEDENCES[] = { -1, 1, 0, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 5 };

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
/*
 * Makes and returns a string expr of specified [length] allocating in the compiler temp allocator. 
 */
static expr_t new_expr_string(compiler_t *c, uint length)
{
   return (expr_t) {
      EXPR_STRING,
      .value.string.length = length,
      .value.string.data = linear_allocator_alloc(&c->temp_allocator, c->allocator, length + 1, 1),
   };
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
 * Returns an "end of statement" token is found (newline, ';', '}' or
 * end-of-stream) and "eats" it.
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

static uint push_scope_identifier(compiler_t * c, const char *id, uint id_len)
{
   ++id_len; /* include null byte */ 
   char *dest_id = seqary_push_n(&c->scope_identifiers, &c->scope_identifiers_size, c->allocator,
                                 char, id_len);
   memcpy(dest_id, id, id_len);
   return (uint) (dest_id - c->scope_identifiers); /* compute and return the offset of the newly
                                                      pushed identifier */
}

/*------------------------------------------------------------------------------------------------*/
/* parsing functions                                                                              */
/*------------------------------------------------------------------------------------------------*/

static expr_t parse_primary_expression(compiler_t *c)
{
   source_location_t source_loc = c->lexer.source_location;
   expr_t expr;
   
   switch (c->lexer.lookahead)
   {
      /* literals */
      case kw_nil: lex(c); expr = (expr_t) { EXPR_NIL }; break;
      case kw_true: lex(c); expr = (expr_t) { EXPR_BOOL, { .boolean = true } }; break;
      case kw_false: lex(c); expr = (expr_t) { EXPR_BOOL, { .boolean = false } }; break;
      case tok_number:
         lex(c);
         expr = (expr_t) { EXPR_NUMBER, { .number = c->lexer.token_number } };
         break;
   }
   
   return expr;
}

static expr_t compile_binary_expression(compiler_t *c, expr_state_t *es,
                                        source_location_t op_source_loc, binop_t op, expr_t lhs,
                                        expr_t rhs)
{
   #define DEFAULT_ARITH_BINOP(opchar)                                                             \
         if (lhs.type <= EXPR_BOOL   || rhs.type <= EXPR_BOOL) goto error;                         \
         if (lhs.type == EXPR_STRING || lhs.type == EXPR_STRING) goto error;                       \
         if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER) {                                 \
            return (expr_t) { EXPR_NUMBER, { .number = lhs.value.number + rhs.value.number } };    \
         }                                                                                         

   /* make a binary operator between our lhs and the rhs; */
   switch (op) {
      case BINOP_ADD:
         /* string concatenation */
         if (lhs.type == EXPR_STRING && rhs.type == EXPR_STRING) {
            expr_t e = new_expr_string(c, lhs.value.string.length + rhs.value.string.length);
            memcpy(e.value.string.data, lhs.value.string.data, lhs.value.string.length);
            memcpy(e.value.string.data + lhs.value.string.length, rhs.value.string.data,
                   rhs.value.string.length + 1);
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
            const uint rhs_int = (uint)rhs.value.number;
            const uint string_length = lhs.value.string.length * rhs_int;
            expr_t e = new_expr_string(c, string_length + 1);
            for (uint offset = 0; offset < string_length; offset += lhs.value.string.length) {
               memcpy(e.value.string.data, lhs.value.string.data + offset, lhs.value.string.length);
            }
            e.value.string.data[string_length] = '\0';
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
         if (lhs.type == EXPR_NIL || rhs.type == EXPR_NIL) goto error;
         if (expr_is_constant(lhs.type) && expr_is_constant(rhs.type)) {
            if (lhs.type == EXPR_BOOL || rhs.type == EXPR_BOOL) goto error;
            if (lhs.type == EXPR_REAL || rhs.type == EXPR_REAL) goto error;
            lhs.integer %= rhs.integer;
            return lhs;
         }
         break;

      /*
       * lhs is a register and we assume that Compiler has called "prepare_logical_operator_lhs" before
       * calling this hence the testset instruction has already been emitted.
       */
      case KC_BINOP_LOGICAL_AND:
         lhs = (expr_is_bool_convertible(lhs.type) && !expr_to_bool(lhs)) ? expr_boolean(false) : rhs;
         return lhs;

      case KC_BINOP_LOGICAL_OR:
         lhs = (expr_is_bool_convertible(lhs.type) && expr_to_bool(lhs)) ? expr_boolean(true) : rhs;
         return lhs;

      case KC_BINOP_EQ:
      case KC_BINOP_NEQ:
      {
         kj_bool invert = binop == KC_BINOP_NEQ;
         if (lhs.type == EXPR_NIL || rhs.type == EXPR_NIL) {
            lhs = expr_boolean(((lhs.type == EXPR_NIL) == (rhs.type == EXPR_NIL)) ^ invert);
            return lhs;
         }

         if (expr_is_constant(lhs.type) && expr_is_constant(rhs.type)) {
            if ((lhs.type == EXPR_BOOL) != (rhs.type == EXPR_BOOL))
               goto error;
            if (lhs.type == EXPR_BOOL)

               lhs = expr_boolean((lhs.integer == rhs.integer) ^ invert);
            if (lhs.type == EXPR_REAL || rhs.type == EXPR_REAL)
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

      default: break;
   }

   /* if we get here, lhs or rhs is a register, the binary operation instruction must be omitted. */
   expr_t lhs_reg = expr_to_any_register(c, lhs, c->temporaries);

   /* if lhs is using the current free register (example, it's constant that has been moved to the free register because its */
   /* index is too large), create a new state copy using the next register */
   int old_temps = use_temporary(c, &lhs_reg);

   expr_t rhs_reg = expr_to_any_register(c, rhs, c->temporaries);

   c->temporaries = old_temps;

   static const expr_type_t COMPARISON_BINOP_TOEXPR_TYPE[] = {
      EXPR_EQ, EXPR_EQ, EXPR_LT, EXPR_LTE, EXPR_LTE, EXPR_LT
   };

   static const kj_bool BINOP_COMPARISON_TEST_VALUE[] = { true, false, true, true, false, false };

   if (binop >= KC_BINOP_EQ && binop <= KC_BINOP_GTE) {
      expr_type_t etype = COMPARISON_BINOP_TOEXPR_TYPE[binop - KC_BINOP_EQ];
      kj_bool positive = BINOP_COMPARISON_TEST_VALUE[binop - KC_BINOP_EQ];
      lhs = expr_comparison(etype, positive, lhs_reg.location, rhs_reg.location);
   }
   else {
      emit(c, encode_ABC(binop - KC_BINOP_ADD + OP_ADD, es->target_register, lhs_reg.location, rhs_reg.location));
      lhs = expr_register(es->target_register);
   }

   return lhs;

   /* the binary operation between lhs and rhs is invalid */
error:
   error(c->lex->issue_handler, sourceloc, "cannot make binary operation '%s' between values"
      " of type '%s' and '%s'.", KC_BINOP_TO_STR[binop], EXPR_TYPE_TO_STRING[lhs.type], EXPR_TYPE_TO_STRING[rhs.type]);
   return (expr_t) { EXPR_NIL };

#undef MAKEBINOP
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

static expr_t parse_expression(compiler_t *c)
{
   /* store the source location for error reporting */
   source_location_t source_loc = c->lexer.source_location;
   
   /* parse expression lhs */
   expr_t lhs = parse_primary_expression(c);

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
   
   return parse_binary_expression_rhs(c, lhs, 0);

error_lhs_not_assignable:
   error(c->lexer.issue_handler, source_loc, "lhs of assignment is not an assignable expression."); 
   return (expr_t) { EXPR_NIL };
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
         syntax_error(c);
         break;
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
