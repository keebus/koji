/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kcompiler.h"
#include "kplatform.h"
#include "kbytecode.h"
#include "klexer.h"
#include "kvalue.h"
#include "kstring.h"

#include <string.h>

 /* A register value. */
typedef int32_t loc_t;

/*
 * The scratch buffer is used by the compiler temporary, linear objects
 * needed for compilation bookkeeping such as the locals in a prototype.
 * This is a simple stack allocator within single pages linked together.
 */
struct scratch_page {
   struct scratch_page *next; /* pointer to the next page */
   char *end; /* end of the buffer */
   char buffer[]; /* begin of the buffer */
};

/*
 * Marks a byte position [cur] marking the next free byte within [page] scratch
 * page. Used to record the current state of the compiler scratch allocator
 * to be restored later when latest temporary allocations are no longer needed.
 */
struct scratch_pos {
   struct scratch_page *page; /* current page with free space */
   char *pos; /* first free byte within the page */
};


/* A local variable is simply a named location on the VM local value stack. */
struct local {
   struct local *next; /* points to the next local */
   loc_t loc; /* location of this local */
   char id[]; /* local identifier */
};

/*
 */
struct branch {
   struct branch *next;
   int32_t instridx;
};

struct protoinfo {
   int32_t instrs_beg;
   int32_t instrs_end;  /* curr prototype number of instructions */
   int32_t consts_beg;
   int32_t consts_end;  /* number of constants in curr prototype */
   int32_t protos_beg;
   int32_t protos_end;
   int32_t nlocals;  /* number of locals in curr prototype */
   int32_t nregs;
   loc_t temp; /* index of the next free register for locals */
   struct local *locals; /* array of local variables */
   struct branch *branch_true; /* list of branches that eval to true */
   struct branch *branch_false; /* list of branches that eval to false */
};

/* Wraps state for a compilation run. */
struct compiler {
   struct lex lex; /* the lexer used to scan tokens from input source */
   struct class *cls_string; /* string class */
   instr_t *instrs;  /* buffer array of prototype instructions */
   int32_t instrs_len; /* capacity of the instrs buffer */
   union value *consts; /* buffer of constants of this prototype */
   int32_t consts_len;   /* capacity of the consts buffer */
   struct prototype **protos; /* array of child prototypes */
   int32_t protos_len;
   struct scratch_page *scratchhead; /* first page in the scratch buffers */
   struct scratch_page *scratchtail; /* last page in the scratch buffer chain*/
   struct scratch_pos scratchpos; /* scratch buffer cursor */
   struct protoinfo pi;
};

/*
 * Type of expressions.
 */
enum expr_type {
   EXPR_NIL,      /* a nil expression */
   EXPR_BOOL,     /* a boole expression */
   EXPR_NUMBER,   /* a numerical expression */
   EXPR_LOCATION, /* a loc expression */
   EXPR_EQ,       /* a equals logical expression */
   EXPR_LT,       /* a less-than logical expression */
   EXPR_LTE,      /* a less-than-equal logical expression */
};

/*
 * Expression type to strings.
 */
static const char *EXPR_TYPE_TO_STRING[] = {
   "nil", "bool", "number", "string", "local", "bool", "bool", "bool"
};

/* comparison expression between lhs and rhs */
struct expr_compare {
   loc_t lhs;
   loc_t rhs;
};

/*
 * Union of valid expression values data.
 */
union expr_value {
   bool boole;
   koji_number_t num;
   struct string *str;
   loc_t loc;
   struct expr_compare comp;
};

/*
 * Wraps info about an expression being compiled. Any unary or binary operation
 * on cnst expressions (e.g. a string or a number) are immediately resolved
 * to their result. If such operations involve one or more expressions whose
 * value is only known at runtime (i.e. a location expr) then the corresponding
 * operation instruction is emitted. Expression also have a [positive] flag
 * which starts true and is inverted every time the expression is negated or
 * when applying De Morgan to bool operations.
 */
struct expr {
   enum expr_type type : 16; /* this expression type */
   bool positive; /* whether this expression is negated (positive==false) */
   union expr_value val; /* this expression value */
};

/*
 * Enumeration of all binary operators.
 */
enum binop {
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
};

/*
 * Binary operator precedences (the lower the number the higher the
 * precedence).
 */
static const int32_t BINOP_PRECEDENCE[] = {
   /* invalid   *   /   %   +  -  << >> &  ^  5  <  <= >  >= == != && || */
   -1,          10, 10, 10, 9, 9, 8, 8, 7, 6, 5, 4, 4, 4, 4, 3, 3, 2, 1
};

/*
 * Binary operator to string table.
 */
static const char *BINOP_TO_STR[] = {
   "<invalid>", "*",  "/", "%", "+", "-", "<<", ">>", "&", "|", "^", "<", "<=",
   ">", ">=", "==", "!=", "&&", "||"
};

/*
 * Binary operator to opcode table.
 */
static const enum opcode BINOP_TO_OPCODE[] = {
   0, OP_MUL, OP_DIV, OP_MOD, OP_ADD, OP_SUB
};

/*
 * A state structure used to contain information about expression parsing and
 * compilation such as the desired target register, whether the expression
 * should be negated and the indices of new branches in compiler state
 * global true/false branches.
 */
struct expr_state {
   struct branch *branch_true; /* index of the first branch to true in exprstate*/
   struct branch *branch_false; /* index to the first branch to false  */
   bool negated; /* whether this expr is negated */
};

/* scratch buffer */
#define SCRATCH_BUFFER_PAGE_SIZE 4096

static char *
align(char *ptr, int32_t align)
{
	const uintptr_t align_minus_one = align - 1;
	return (char*)(((uintptr_t)ptr + align_minus_one) & ~align_minus_one);
}

static int32_t
scratch_size(struct scratch_page *p)
{
   return (int32_t)(p->end - p->buffer);
}

static struct scratch_page *
scratch_page_new(struct compiler *c, int32_t size)
{
   int32_t pagesz = max_i32(SCRATCH_BUFFER_PAGE_SIZE,
      sizeof(struct scratch_page) + size);      

   struct scratch_page *page = c->lex.alloc.alloc(pagesz, c->lex.alloc.user);
   page->next = c->scratchpos.page->next;
   page->end = (char*)page + pagesz;
         
   c->scratchpos.page = page;
   c->scratchpos.pos = page->buffer;

   c->scratchpos.page->next = page;

   return page;
}

static void
scratch_init(struct compiler *c)
{
   struct scratch_page *page = c->lex.alloc.alloc(SCRATCH_BUFFER_PAGE_SIZE,
      c->lex.alloc.user);
   page->next = NULL;
   page->end = (char*)page + SCRATCH_BUFFER_PAGE_SIZE;

   c->scratchpos.page = page;
   c->scratchpos.pos = page->buffer;

   c->scratchhead = c->scratchtail = page;
}

static void
scratch_deinit(struct compiler *c)
{
   struct scratch_page *page = c->scratchhead;
   while (page) {
      struct scratch_page *next = page->next;
      c->lex.alloc.free(page, (int)(page->end - (char*)page), c->lex.alloc.user);
      page = next;
   }
}

static void *
scratch_alloc(struct compiler *c, int32_t size, int32_t alignm)
{
   struct scratch_pos *spos = &c->scratchpos;
   char *ptr = align(spos->pos, alignm);
   if (ptr + size >= spos->page->end) {
      if (spos->page->next) {
         if (scratch_size(spos->page) >= size) {
            /* next page has enough space to hold allocation */
            spos->page = spos->page->next;
            spos->pos = spos->page->buffer;
         }
         else {
            /* next page does not hold enough space for allocation; push new
               page between the two */
            scratch_page_new(c, size);
         }
      }
      else {
          /* put to the end */
         struct scratch_page *page = scratch_page_new(c, size);
         c->scratchtail->next = page;
         c->scratchtail = page;
      }
      return scratch_alloc(c, size, alignm);
   }
   spos->pos = ptr + size;
   return ptr;
}

#define scratch_alloct(c, type)\
   ((type *)scratch_alloc(c, sizeof(type), kalignof(type)))

/*
 * Returns whether [l] is a constant location.
 */
static bool
loc_is_const(loc_t l)
{
   return l < 0;
}

/*
 * Returns whether [l] is a temporary location, i.e. neither a constant nor a
 * local.
 */
static bool
loc_is_temp(struct compiler *c, loc_t l)
{
   return l >= (loc_t)c->pi.nlocals;
}

/* expression handling */

/*
 * Returns a nil expr.
 */
static struct expr
expr_nil(void)
{
   struct expr e;
   e.type = EXPR_NIL;
   e.positive = true;
   return e;
}

/*
 * Makes and returns a number expr of specified [value].
 */
static struct expr
expr_bool(bool value)
{
   struct expr e;
   e.type = EXPR_BOOL;
   e.positive = true;
   e.val.boole = value;
   return e;
}

/*
 * Makes and returns a bool expr of specified [value].
 */
static struct expr
expr_num(koji_number_t value)
{
   struct expr e;
   e.type = EXPR_NUMBER;
   e.positive = true;
   e.val.num = value;
   return e;
}

/*
 * Makes and returns a location expr of specified [value].
 */
static struct expr
expr_loc(loc_t value)
{
   struct expr e;
   e.type = EXPR_LOCATION;
   e.positive = true;
   e.val.loc = value;
   return e;
}

/*
 * Makes and returns a comparison expr of specified [type] between [lhsloc]
 * and [lhsloc] against test value [testval].
 */
static struct expr
exprcompare(enum expr_type type, bool testval, int32_t lhsloc, int32_t rhsloc)
{
   struct expr e;
   e.type = type;
   e.positive = testval;
   e.val.comp.lhs = lhsloc;
   e.val.comp.rhs = rhsloc;
   return e;
}

/*
 * Returns whether expression is a cnst (i.e. nil, bool, number or
 * string).
 */
static bool
expr_isconst(enum expr_type type)
{
   return type <= EXPR_NUMBER;
}

/*
 * Converts [expr] to a bool. 'expr_isconst(expr)' must return true.
 */
static bool
expr_tobool(struct expr expr)
{
   switch (expr.type) {
      case EXPR_NIL:
         return 0;

      case EXPR_BOOL:
      case EXPR_NUMBER:
         return expr.val.num != 0;

      default:
         assert(0);
         return 0;
   }
}

/*
 * Returns whether an expression of specified [type] is a comparison.
 */
static bool
expr_iscompare(enum expr_type type)
{
   return type >= EXPR_EQ;
}

/*
 * Applies a (logical) negation to the expression. It returns the negated
 * result.
 */
static struct expr
expr_negate(struct expr e)
{
   switch (e.type) {
      case EXPR_NIL:    return expr_bool(true);
      case EXPR_BOOL:   return expr_bool(!e.val.boole);
      case EXPR_NUMBER: return expr_bool(!expr_tobool(e));
      default:
         e.positive = !e.positive;
         return e;
   }
}

/* parsing helpers */

/*
 * Formats and reports a syntax error (unexpected <token>) at specified source
 * location.
 */
static void
error_syntax_at(struct compiler *c, struct sourceloc sloc)
{
   error(c->lex.issue_handler, sloc, "unexpected '%s'.",
      lex_tok_ahead_pretty_str(&c->lex));
}

/*
 * Calls [syntax_error_at] passing current lex source location.
 */
static void
error_syntax(struct compiler *c)
{
   error_syntax_at(c, c->lex.sourceloc);
}

/*
 * Returns whether the current lookahead is [tok].
 */
static bool
peek(struct compiler *c, token_t tok)
{
   return c->lex.tok == tok;
}

/*
 * Tells the compiler lex to scan the next lookahead.
 */
static token_t
lex(struct compiler *c)
{
   return lex_scan(&c->lex);
}

/*
 * Scans next token if lookahead is [tok]. Returns whether a new token was
 * scanned.
 */
static bool
accept(struct compiler *c, token_t tok)
{
   if (peek(c, tok)) {
      lex(c);
      return true;
   }
   return false;
}

/*
 * Reports a compilation error if lookahead differs from [tok].
 */
static void
check(struct compiler *c, token_t tok)
{
   if (!peek(c, tok)) {
      char buffer[64];
      error(c->lex.issue_handler, c->lex.sourceloc,
         "missing %s before '%s'.", lex_tok_pretty_str(tok, buffer, 64),
         lex_tok_ahead_pretty_str(&c->lex));
   }
}

/*
 * Checks that lookahead is [tok] then scans next token.
 */
static void
expect(struct compiler *c, token_t tok)
{
   check(c, tok);
   lex(c);
}

/*
 * Returns an "end of statement" token is found (newline, ';', '}' or end-of-
 * stream) and "eats" it.
 */
static bool
accept_endofstmt(struct compiler *c)
{
   if (accept(c, ';') || c->lex.tok == '}' || c->lex.tok == tok_eos)
      return true;
   if (c->lex.newline) {
      c->lex.newline = false;
      return true;
   }
   return false;
}

/*
 * Expects an end of statement.
 */
static void
expect_endofstmt(struct compiler *c)
{
   if (!accept_endofstmt(c))
      error_syntax(c);
}


/* compilation helper functions */

/*
 * Pushes an offset to the [branch] and returns a pointer to its instr index.
 */
static int32_t *
branch_push(struct compiler *c, struct branch **head)
{
   struct branch *br = scratch_alloct(c, struct branch);
   br->next = *head;
   *head = br;
   return &br->instridx;
}

/*
 * Writes the offset to branches contained in [branch] starting from
 * [firstidx] to target instruction constidx target_index.
 */
static void
branches_bind(struct compiler *c, struct branch **begin, struct branch *end,
   int32_t instridx)
{
   for (struct branch *br = *begin; br != end; br = br->next)
      replace_Bx(c->instrs + br->instridx, instridx - br->instridx - 1);
   *begin = end;
}

/*
 * Binds branches in [branch] starting from [firstidx] to the next
 * instruction that will be emitted to current prototype.
 */
static void
branches_bind_here(struct compiler *c, struct branch **begin,
   struct branch *end)
{
   branches_bind(c, begin, end, c->pi.instrs_end);
}

/*
 * Converts token [tok] to the corresponding binary operator.
 */
static enum binop
tok_to_binop(token_t tok)
{
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
      /*
      case '&':  return BINOP_BIT_AND;
      case '|':  return BINOP_BIT_OR;
      case '^':  return BINOP_BIT_XOR;
      case '<<': return BINOP_BIT_LSH;
      case '>>': return BINOP_BIT_RSH;
      */
      default:   return BINOP_INVALID;
   }
}

/*
 * Makes a new expression state and returns it using [negated] to mark whether
 * expression is negated and holding the index of the first next free branch
 * instruction slot in both branches true/false in the compiler so that next
 * branches to either the true or the false branch will belong to
 * returned expression state.
 */
static struct expr_state
make_exprstate(struct compiler *c, bool negated)
{
   struct expr_state es;
   es.branch_true = c->pi.branch_true;
   es.branch_false = c->pi.branch_false;
   es.negated = negated;
   return es;
}

/*
 * If expression [e] is a register of location equal to current free register,
 * it bumps up the free register counter. It returns the old temporary register
 * regardless whether if the current temporary register was bumped up. After
 * using the new temporary you must restore the temporary register [c->pi.temp] to
 * the value returned by this function.
 */
static int32_t
use_temp(struct compiler *c, struct expr const *e)
{
   int32_t oldtemp = c->pi.temp;
   if (e->type == EXPR_LOCATION && e->val.loc == c->pi.temp)
      ++c->pi.temp;
   return oldtemp;
}

/*
 * Pushes instruction [i] to current prototype instructions.
 */
static void
emit(struct compiler *c, instr_t instr)
{
   /* if instruction has target, update the current prototype total number of
      used registers */
   if (opcode_has_target(decode_op(instr)))
      c->pi.nregs = max_i32(c->pi.nregs, decode_A(instr) + 1);

   *array_push(&c->instrs, &c->pi.instrs_end, &c->instrs_len, &c->lex.alloc, loc_t, 1)
      = instr;
}

/*
 * Defines a new local variable in current prototype with identifier [id].
 * Returned new local must be pushed through [local_push()].
 */
static struct local*
local_alloc(struct compiler *c, char *id, int32_t idlen)
{
   struct local *local = scratch_alloc(c, sizeof(struct local) + idlen + 1,
      kalignof(struct local));
   memcpy(local->id, id, idlen + 1);
   return local;
}

/*
 * Pushes a previously allocated local through [local_alloc] after setting its
 * location to the current free temporary and bumps this up.
 */
static void
local_push(struct compiler *c, struct local *local)
{
   local->loc = c->pi.temp++;
   local->next = c->pi.locals;
   c->pi.locals = local;
   ++c->pi.nlocals;
}

/*
 * Finds a local with matching [identifier] starting from the current scope and
 * iterating over its parents until. If none could be found it returns NULL.
 */
static struct local *
local_fetch(struct compiler *c, const char *id)
{
   for (struct local *local = c->pi.locals; local; local = local->next) {
      if (strcmp(local->id, id) == 0)
         return local;
   }
   return NULL;
}

/*
 * Converts a constant index [constidx] into a location expression to the
 * constant. This might be a _direct_ index (e.g. -3) if [constidx] is small
 * enough, otherwise to a temporary into which the constant is moved with a
 * move instruction.
 */
static struct expr
expr_const(struct compiler *c, loc_t target_hint, int32_t constidx)
{
   loc_t loc = -(int32_t)constidx - 1;
   if (constidx <= MAX_ABC_VALUE) {
      /* cnst is small enough to be used as direct index */
      return expr_loc(loc);
   }
   else {
      /* cnst too large, load it into a temporary register */
      emit(c, encode_ABx(OP_MOV, target_hint, loc));
      return expr_loc(target_hint);
   }
}

/*
 * Fetches or defines if not found a real cnst [num] and returns its index.
 */
static struct expr
const_fetch_num(struct compiler *c, loc_t target_hint, koji_number_t num)
{
   union value value = value_num(num);
   int32_t constidx;

   for (int32_t i = 0; i < c->pi.consts_end; ++i) {
      if (c->consts[i].bits == value.bits) {
         constidx = i; /* constant already existent */
         goto done;
      }
   }

   /* constant not found, add it */
   union value *cnst = array_seq_push(&c->consts, &c->pi.consts_end,
      &c->lex.alloc, union value, 1);
   
   *cnst = value;
   constidx = (int32_t)(cnst - c->consts);

done:
   return expr_const(c, target_hint, constidx);
}

/*
 * Fetches or defines if not found a string cnst [str] and returns its
 * constant index.
 */
static struct expr
const_fetch_str(struct compiler *c, loc_t target_hint, const char *chars,
   int32_t len)
{
   union value *cnst;
   int32_t constidx;

   for (int32_t i = 0; i < c->pi.consts_end; ++i) {
      cnst = c->consts + i;

      /* is i-th cnst a string and do the strings match? if so, no need to
         add a new constant */
      if (!value_isobj(*cnst))
         continue;

      struct string *string = value_getobjv(*cnst);
      if (string->object.class != c->cls_string)
         continue;

      if (string->len == len && memcmp(string->chars, chars, len) == 0) {
         constidx = i;
         goto done;
      }
   }

   /* cnst not found, push the new cnst to the array */
   cnst = array_seq_push(&c->consts, &c->pi.consts_end, &c->lex.alloc, union value,
                         1);

   /* create a new string */
   *cnst = value_new_string(c->cls_string, &c->lex.alloc, len);

   struct string *string = value_getobjv(*cnst);
   memcpy(string->chars, chars, len + 1);
   assert(string->chars[len] == 0);

   constidx = (int32_t)(cnst - c->consts);

done:
   return expr_const(c, target_hint, constidx);
}

/*
 * If expression [e] is not of type `EXPR_LOCATION` this function emits a
 * sequence of instructions so that the value or result of [e] is written to
 * location [target_hint]. If [e] is already oftype `EXPR_LOCATION` this
 * function does nothing. In either case, a location expression is returned
 * with its location value set to the local that will contain the compiled
 * expression value.
 */
static struct expr
expr_compile(struct compiler *c, struct expr e, int32_t target_hint)
{
   switch (e.type) {
      case EXPR_NIL:
         emit(c, encode_ABx(OP_LOADNIL, target_hint, target_hint));
         return expr_loc(target_hint);

      case EXPR_BOOL:
         emit(c, encode_ABC(OP_LOADBOOL, target_hint, e.val.boole, 0));
         return expr_loc(target_hint);

      case EXPR_NUMBER:
         return const_fetch_num(c, target_hint, e.val.num);

      case EXPR_LOCATION:
         if (e.positive) return e;
         emit(c, encode_ABx(OP_NEG, target_hint, e.val.loc));
         return expr_loc(target_hint);

      case EXPR_EQ:
      case EXPR_LT:
      case EXPR_LTE:
         /* compile the comparison expression to a sequence of instructions
            that write into [target_hint] the value of the comparison */
         emit(c, encode_ABC(OP_EQ + e.type - EXPR_EQ, e.val.comp.lhs,
            e.positive, e.val.comp.rhs));
         emit(c, encode_ABx(OP_JUMP, 0, 1));
         emit(c, encode_ABC(OP_LOADBOOL, target_hint, false, 1));
         emit(c, encode_ABC(OP_LOADBOOL, target_hint, true, 0));
         return expr_loc(target_hint);

      default:
         assert(!"Unreachable");
         return expr_nil();
   }
}

/*
 * Compiles the unary minus of expression [e] and returns the result.
 */
static struct expr
expr_compile_unary(struct compiler *c, struct sourceloc sloc, struct expr e)
{
   switch (e.type) {
      case EXPR_NUMBER:
         return expr_num(-e.val.num);

      case EXPR_LOCATION:
         emit(c, encode_ABx(OP_UNM, c->pi.temp, e.val.loc));
         return expr_loc(c->pi.temp);

      default:
         error(c->lex.issue_handler, sloc,
            "cannot apply operator unary minus to a value of type %s.",
            EXPR_TYPE_TO_STRING[e.type]);
   }
   return expr_nil();
}

/*
 * Helper function for parse_binary_expr_rhs() that actually compiles the
 * binary operation between [lhs] and [rhs]. This function also checks whether
 * the operation can be optimized to a constant if possible before falling back
 * to emitting the actual instructions.
 */
static struct expr
expr_compile_binary(struct compiler *c, struct sourceloc op_sloc,
   enum binop op, struct expr lhs, struct expr rhs)
{
#define DEFAULT_ARITH_INVALID_OPS_CHECKS()\
   if (lhs.type <= EXPR_BOOL   || rhs.type <= EXPR_BOOL)    goto error;\

#define DEFAULT_ARITH_BINOP(opchar)\
   DEFAULT_ARITH_INVALID_OPS_CHECKS();\
   if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER) {\
      return expr_num(lhs.val.num opchar rhs.val.num);\
   }

   union expr_value lval = lhs.val;
   union expr_value rval = rhs.val;

   /* make a binary operator between our lhs and the rhs; */
   switch (op) {
      case BINOP_ADD:
         /* skip the DEFAULT_ARITH_BINOP as it throws an error if any operand
            is a string, but no error should be thrown if we're adding a string
            to a location as we don't know what type the value at that location
            will have at runtime */
         if (lhs.type == EXPR_LOCATION || rhs.type == EXPR_LOCATION)
            break;
         DEFAULT_ARITH_BINOP(+);
         break;

      case BINOP_MUL:
         /* same reasons as for the BINOP_ADD case */
         if (lhs.type == EXPR_LOCATION || rhs.type == EXPR_LOCATION)
            break;
         DEFAULT_ARITH_BINOP(*);
         break;

      case BINOP_SUB: DEFAULT_ARITH_BINOP(-); break;
      case BINOP_DIV: DEFAULT_ARITH_BINOP(/ ); break;

      case BINOP_MOD:
         DEFAULT_ARITH_INVALID_OPS_CHECKS();
         if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER) {
            int64_t result = (int64_t)lval.num % (int64_t)rval.num;
            return expr_num((koji_number_t)result);
         }
         break;

      /* lhs is a register and we assume that the compiler has called
         [prepare_logical_operator_lhs] before calling this hence the TESTSET
         instruction has already been emitted. */
      case BINOP_LOGICAL_AND:
         return (expr_isconst(lhs.type) && !expr_tobool(lhs))
            ? expr_bool(false)
            : rhs;

      case BINOP_LOGICAL_OR:
         return (expr_isconst(lhs.type) && expr_tobool(lhs))
            ? expr_bool(true)
            : rhs;

      case BINOP_EQ:
      case BINOP_NEQ:
      {
         const bool invert = (op == BINOP_NEQ);
         if (lhs.type == EXPR_NIL || rhs.type == EXPR_NIL) {
            bool result = (lhs.type == EXPR_NIL) == (rhs.type == EXPR_NIL);
            return expr_bool(result ^ invert);
         }
         else if (expr_isconst(lhs.type) && expr_isconst(rhs.type)) {
            if (lhs.type == EXPR_BOOL && rhs.type == EXPR_BOOL) {
               return expr_bool((lval.boole == rval.boole) ^ invert);
            }            
            else if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER) {
               return expr_bool((lval.num == rval.num) ^ invert);
            }
            goto error;
         }
         break;
      }

      case BINOP_LT:
      case BINOP_GTE:
      {
         bool invert = (op == BINOP_GTE);
         if (lhs.type == EXPR_NIL)
            return expr_bool((rhs.type == EXPR_NIL) == invert);
         else if (rhs.type == EXPR_NIL)
            return expr_bool((lhs.type == EXPR_NIL) != invert);
         else if (expr_isconst(lhs.type) && expr_isconst(rhs.type)) {
            if (lhs.type == EXPR_BOOL && rhs.type == EXPR_BOOL) {
               return expr_bool((lval.boole < rval.boole) ^ invert);
            }
            else if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER) {
               return expr_bool((lval.num < rval.num) ^ invert);
            }
            goto error;
         }
         break;
      }

      case BINOP_LTE:
      case BINOP_GT:
      {
         bool invert = (op == BINOP_GT);
         if (lhs.type == EXPR_NIL)
            return expr_bool((rhs.type == EXPR_NIL) == invert);
         if (rhs.type == EXPR_NIL)
            return expr_bool((lhs.type == EXPR_NIL) != invert);
         if (expr_isconst(lhs.type) && expr_isconst(rhs.type)) {
            if (lhs.type == EXPR_BOOL && rhs.type == EXPR_BOOL) {
               return expr_bool((lval.boole <= rval.boole) ^ invert);
            }
            else if (lhs.type == EXPR_NUMBER && rhs.type == EXPR_NUMBER) {
               return expr_bool((lval.num <= rval.num) ^ invert);
            }
            goto error;
         }
         break;
      }

      default: break;
   }

   /* if we get here, lhs or rhs is a register, the binary operation
      instruction must be omitted. */
   {
      loc_t oldtemp; /* backup the num of temporaries */

      lhs = expr_compile(c, lhs, c->pi.temp); /* compile the lhs to some reg */

      /* if lhs is using the current temporary (e.g., it's cnst that has been
         moved to the free temporary because its index is too large), mark
         the temporary as used and remember the old temporary location to be
         restored later */
      oldtemp = use_temp(c, &lhs);

      /* compile the expression rhs to a register as well using a potentially
         new temporary */
      rhs = expr_compile(c, rhs, c->pi.temp);

      /* both lhs and rhs are now compiled to registers, restore the old
         temporary location */
      c->pi.temp = oldtemp;

      /* if the binary operation is a comparison generate and return a
         comparison expression */
      if (op >= BINOP_LT && op <= BINOP_NEQ) {
         /* maps binary operators to comparison expression types */
         static const enum expr_type COMPARISON_BINOP_TO_exprtype[] = {
            /* lt       lte       gt        gte      eq        neq */
               EXPR_LT, EXPR_LTE, EXPR_LTE, EXPR_LT, EXPR_EQ, EXPR_EQ
         };

         /* maps binary operators to comparison expression testing values */
         static const bool COMPARISON_BINOP_TO_TEST_VALUE[] = {
            /* lt    lte   gt     gte    eq    neq */
               true, true, false, false, true, false
         };

         enum expr_type exprtype = COMPARISON_BINOP_TO_exprtype[op - BINOP_LT];
         bool       positive = COMPARISON_BINOP_TO_TEST_VALUE[op - BINOP_LT];

         lhs = exprcompare(exprtype, positive, lhs.val.loc, rhs.val.loc);
      }
      else {
         /* the binary operation is not a comparison but an arithmetic
            operation, emit the appropriate instruction */
         emit(c, encode_ABC(BINOP_TO_OPCODE[op], c->pi.temp, lhs.val.loc,
            rhs.val.loc));
         lhs = expr_loc(c->pi.temp);
      }

      return lhs;
   }

error:  /* the binary operation between lhs and rhs is invalid */
   error(c->lex.issue_handler, op_sloc, "cannot make binary operation '%s' "
      "between values of type '%s' and '%s'.", BINOP_TO_STR[op],
      EXPR_TYPE_TO_STRING[lhs.type], EXPR_TYPE_TO_STRING[rhs.type]);
   return expr_nil();

#undef MAKEBINOP
}

/*
 * Computes and returns the offset specified from instruction [from_instr_idx]
 * to the next instruction that will be emitted in current prototype.
 */
static int32_t
offset_to_next_instr(struct compiler *c, int32_t from_instr_idx)
{
   return c->pi.instrs_end - from_instr_idx - 1;
}

/*
 * Compiles the lhs of a logical expression if [op] is such and [lhs] is a
 * register or comparison. In a nutshell, the purpose of this function is to
 * patch the current early out branches to the true or false branch depending on
 * [op], the truth value (positivity) of lhs and whether the whole
 * expression should be negated (as stated by [es]).
 * What this function does is: compiles the comparison if lhs is one, or emit
 * the test/testset if is a register if say op is an OR then it patches all
 * existing branches to false in the compiled expression represented by lhs to
 * this point so that the future rhs will "try again" the OR. The opposite
 * holds for AND (jumps to true are patched to evaluate the future rhs because
 * evaluating only the lhs to true is not enough in an AND).
 */
static void
compile_logical_op(struct compiler *c, struct expr_state *es, enum binop op,
   struct expr lhs)
{
   /* do nothing if lhs is not a location or comparison or if the comparison
      is not an and/or */
   if ((lhs.type != EXPR_LOCATION && !expr_iscompare(lhs.type))
      || (op != BINOP_LOGICAL_AND && op != BINOP_LOGICAL_OR))
      return;

   /* the boolean value to test the expression to */
   bool testval = (op == BINOP_LOGICAL_OR) ^ es->negated;

   /* compile condition */
   switch (lhs.type) {
      case EXPR_LOCATION:
         if ((!lhs.positive) == es->negated) {
            emit(c, encode_ABC(OP_TESTSET, MAX_ABC_VALUE, lhs.val.loc,
               testval));
         }
         else {
            assert(lhs.val.loc >= 0); /* is non-cnst */
            emit(c, encode_ABC(OP_TEST, lhs.val.loc, !testval, 0));
         }
         break;

      case EXPR_EQ: case EXPR_LT: case EXPR_LTE:
      {
         bool res = (lhs.positive ^ es->negated) ^ !testval;
         emit(c, encode_ABC(OP_EQ + lhs.type - EXPR_EQ, lhs.val.comp.lhs,
            lhs.val.comp.rhs, res));
         break;
      }

      default: assert(false);
   }

   /* ptr to an offset entry into the branch jumped */
   /* push branch index to the appropriate branch. */
   int32_t *offset;
   struct branch *branch_end; /* index of the last branch pushed */
   struct branch **branches; /* the branch we need to jump to */

   offset = branch_push(c, testval ? &c->pi.branch_true : &c->pi.branch_false);
   *offset = c->pi.instrs_end;
   emit(c, OP_JUMP);

   /* if we're testing to true then the branch to jump to if evaluation is false
      is the false branch and viceversa */
   if (testval) {
     /* or */
      branches = &c->pi.branch_false;
      branch_end = es->branch_false;

   }
   else {
      /* and */
      branches = &c->pi.branch_true;
      branch_end = es->branch_true;
   }

   while (*branches != branch_end) {
      /* get the index of the last branch in the jump vector */
      int32_t index = (*branches)->instridx;

      /* if instruction before the current branch is a TESTSET, turn
         it to a simple TEST instruction as its "set" is wasted since we need
         to test more locations before we can finally set the target (example
         "c = a && b", if a is true, don't c just yet, we need to test b
         first) */
      if (index > 0 && decode_op(c->instrs[index - 1]) == OP_TESTSET) {
         instr_t  instr = c->instrs[index - 1];
         int32_t      testloc = decode_B(instr);
         bool    flag = (bool)decode_C(instr);
         c->instrs[index - 1] = encode_ABx(OP_TEST, testloc, flag);
      }

      /* affix the jump to this instruction as more testing is needed to
         determine if expression is true or false. */
      replace_Bx(c->instrs + index, offset_to_next_instr(c, index));

      /* shrink the size of the true or false jump vector by one */
      *branches = (*branches)->next;
   }
}

/*
 * It closes an expression, i.e. it compiles it to a location closing any
 * pending jump to true or false branches as stated in [es]. After this func
 * returns all conditional jumps emitted but open part of this expression are
 * resolved, their offsets computed and terminating instructions such as
 * loadbool are emitted. The returned expression is guaranteed to refer to a
 * location, [target_hint] if some temporary location was necessary. If the
 * [movetotarget] flag is true, this function will make sure that
 * the compiled expression lies in [target_hint].
 */
static struct expr
expr_close(struct compiler *c, struct expr_state *es, struct expr expr,
   loc_t target_hint, bool movetotarget)
{
   loc_t targetloc = target_hint;

   /* declare some bookkeeping flags */
   bool value_is_compare = expr_iscompare(expr.type);
   int32_t rhs_move_jump_idx = 0; /* index of the instruction to jump to that
                                     moves rhs to lhs */
   bool set_value_to_true = false; /* emit set target to true? */
   bool set_value_to_false = false; /* emit set target to false? */
   int32_t load_false_instr_idx = 0; /* idx of the instr that loadbools false*/

   if (value_is_compare) {
      union expr_value eval = expr.val;
      int32_t A = OP_EQ + expr.type - EXPR_EQ;
      emit(c, encode_ABC(A, eval.comp.lhs, eval.comp.rhs, expr.positive));
      *branch_push(c, &c->pi.branch_true) = c->pi.instrs_end;
      emit(c, encode_ABx(OP_JUMP, 0, 0));
      set_value_to_false = true;
   }
   else {
      /* compiled expression instance that will hold final instruction location
         and the instructions that ultimately write to that location */
      targetloc = expr_compile(c, expr, target_hint).val.loc;

      if (movetotarget && targetloc != target_hint) {
         /* if from location is a temporary location (i.e. not a local
            variable) and we know from [from] that one or more instructions are
            setting to this location, we can simply replace the A operand of
            all those instructions to [to] and optimize out the 'move'
            instruction */
         if (targetloc >= c->pi.temp) {
            /* first check whether last instruction targets old location, if so
               update it with the new desired location */
            instr_t *instr = &c->instrs[c->pi.instrs_end - 1];
            if (opcode_has_target(decode_op(*instr)) && decode_A(*instr)
               == targetloc) {
              /* target matches result expression target register? if so,
                 replace it with [to] */
               replace_A(instr, target_hint);
            }
         }
         else {
            /* we could not optimize out the move instruction, emit it */
            emit(c, encode_ABx(OP_MOV, target_hint, targetloc));
         }

         /* the expression is forced to be in target_hint, update location */
         targetloc = target_hint;
      }

      if (c->pi.branch_true != es->branch_true &&
          c->pi.branch_false != es->branch_false) {
         goto done;
      }

      rhs_move_jump_idx = c->pi.instrs_end;
      emit(c, encode_ABx(OP_JUMP, 0, 0));
   }

   struct branch *br; /* used for iteration */

   /* iterate over instructions that branch to false and if any is not a
      testset instruction, it means that we need to emit a loadbool instruction
      to set the result to false, so for now just remember this by flagging
      set_value_to_false to true. Also update the target register of the
      TESTSET instruction to the actual target register */
   for (br = c->pi.branch_false; br != es->branch_false; br = br->next) {
      int32_t index = br->instridx;
      if (index > 0) {
         instr_t *instr = &c->instrs[index - 1];
         if (decode_op(*instr) == OP_TESTSET) {
            replace_A(instr, target_hint);
         }
         else {
            set_value_to_false = true;
            replace_Bx(c->instrs + index, offset_to_next_instr(c, index));
         }
      }
   }

   /* if we need to set the result to false, emit the loadbool instruction (to
      false) now and remember its index so that we can eventually patch it
      later */
   if (set_value_to_false) {
      load_false_instr_idx = c->pi.instrs_end;
      emit(c, encode_ABC(OP_LOADBOOL, target_hint, false, 0));
   }

   /* analogous to the false case, iterate over the list of instructions
      branching to true, flag set_value_to_true if instruction is not a
      testset, as we'll need to emit a loadbool to true instruction in such
      case. Also patch all jumps to this point as the next instruction emitted
      could be the loadbool to true. */
   for (br = c->pi.branch_true; br != es->branch_true; br = br->next) {
      int32_t index = br->instridx;
      if (index > 0) {
         instr_t *instr = &c->instrs[index - 1];
         if (decode_op(*instr) == OP_TESTSET) {
            replace_A(instr, target_hint);
         }
         else {
            set_value_to_true = true;
            replace_Bx(c->instrs + index, offset_to_next_instr(c, index));
         }
      }
   }

   /* emit the loadbool instruction to *true* if we need to */
   if (set_value_to_true)
      emit(c, encode_ABC(OP_LOADBOOL, target_hint, true, 0));

   /* if we emitted a loadbool to *false* instruction before, we'll need to
      patch the jump offset to the current position (after the eventual
      loadbool to *true* has been emitted) */
   if (set_value_to_false)
      replace_C(c->instrs + load_false_instr_idx,
         offset_to_next_instr(c, load_false_instr_idx));

/* If the final subexpression was a register, check if we have added any
   loadb instruction. If so, set the right jump offset to this location,
   otherwise pop the last instruction which is the "jump" after the "mov"
   or "neg" to skip the loadbool instructions. */
   if (!value_is_compare) {
      if (!set_value_to_true && !set_value_to_false)
         --c->pi.instrs_end;
      else
         replace_Bx(c->instrs + rhs_move_jump_idx,
            offset_to_next_instr(c, rhs_move_jump_idx));
   }

   /* finally set the jump offset of all remaining TESTSET instructions
      generated by the expression to true... */
   for (br = c->pi.branch_true; br != es->branch_true; br = br->next) {
      int32_t index = br->instridx;
      if (index > 0 && decode_op(c->instrs[index - 1]) == OP_TESTSET)
         replace_Bx(c->instrs + index, offset_to_next_instr(c, index));
   }

   /* ...and to false to the next instruction. */
   for (br = c->pi.branch_false; br != es->branch_false; br = br->next) {
      int32_t index = br->instridx;
      if (index > 0 && decode_op(c->instrs[index - 1]) == OP_TESTSET)
         replace_Bx(c->instrs + index, offset_to_next_instr(c, index));
   }

done: /* restore the compilation state and return result location expression */
   c->pi.branch_true  = es->branch_true;
   c->pi.branch_false = es->branch_false;
   return expr_loc(targetloc);
}

/* parsing functions */
static struct expr
parse_expr(struct compiler *, struct expr_state *);

static struct expr
parse_exprto(struct compiler *, loc_t target_hint, bool movetotarget);

static void
parse_prototype_body(struct compiler *);

static void
parse_block(struct compiler *c);

/*
 * Scans ahead token identifier to a string expression and returns it.
 */
static struct expr
scan_id(struct compiler *c, loc_t target_hint)
{
   assert(c->lex.tok == tok_identifier);
   struct expr expr = const_fetch_str(c, target_hint, c->lex.tokstr,
      c->lex.tokstrlen);
   lex(c);
   return expr;
}

static void
push_prototype(struct compiler *c, int32_t nargs, struct protoinfo const *pi)
{
   int32_t ninstrs = c->pi.instrs_end - c->pi.instrs_beg;
   int32_t nconsts = c->pi.consts_end - c->pi.consts_beg;
   int32_t nprotos = c->pi.protos_end - c->pi.protos_beg;
   struct prototype *p = prototype_new(nconsts, ninstrs, nprotos, &c->lex.alloc);
   p->nargs = nargs;
   p->nregs = c->pi.nregs;
   memcpy(p->instrs, c->instrs + c->pi.instrs_beg, ninstrs * sizeof(*c->instrs));
   memcpy(p->consts, c->consts + c->pi.consts_beg, nconsts * sizeof(*c->consts));
   memcpy(p->protos, c->protos + c->pi.protos_beg, nprotos * sizeof(*c->protos));
   c->pi = *pi;
   *array_push(&c->protos, &c->pi.protos_end, &c->protos_len, &c->lex.alloc,
      struct prototype *, 1) = p;
}

/*
 * Todo
 */
static struct expr
parse_localref_or_call(struct compiler *c, struct expr_state *es)
{
   int32_t idlen;
   struct local *local;
   assert(peek(c, tok_identifier));

   idlen = c->lex.tokstrlen; /* remember identifier length */

   /* temporary remember the identifier as we need to carry on lexing to know
      whether it's a local variable reference or a function call */
   char *id = kalloca(idlen + 1);
   memcpy(id, c->lex.tokstr, idlen + 1);
   lex(c);

   /* identifier refers to local variable? */
   if ((local = local_fetch(c, id))) {
      return expr_loc(local->loc);
   }

   assert(!"todo");
   (void)es;
   return expr_nil();
}


/*
 * Parses and returns a subexpression like "(a + 2)".
 */
static struct expr
parse_subexpr(struct compiler *c, struct expr_state *es)
{
   /* prepare a sub expression state identical to the incoming state */
   struct expr_state sub_es = *es;
   struct expr expr;

   assert(peek(c, '('));
   lex(c); /* eat the '(' */

   /* parse the subexpression with local expression state */
   expr = parse_expr(c, &sub_es);

   expect(c, ')');

   /* if what follows the subexpression is anything but a logical operator
      (and/or) then it the expression must be closed. */
   switch (c->lex.tok) {
      case '+': case '-': case '*': case '/': case '(': case '&': case '|':
      case '[':
         expr = expr_close(c, &sub_es, expr, c->pi.temp, false);
   }

   return expr;
}

/*
 * 
 */
static struct expr
parse_closure(struct compiler *c)
{
   assert(peek(c, kw_func));
   lex(c);

   /* save current prototype state */
   struct protoinfo bak = c->pi;
   c->pi = (struct protoinfo) {
      c->pi.instrs_end, c->pi.instrs_end,
      c->pi.consts_end, c->pi.consts_end,
      c->pi.protos_end, c->pi.protos_end
   };

   int32_t nargs = 0;
   if (accept(c, '(') && !accept(c, ')')) {
      do
      {
         /* push the argument as local */
         check(c, tok_identifier);
         struct local *l = local_alloc(c, c->lex.tokstr, c->lex.tokstrlen);
         local_push(c, l);
         ++nargs;
         lex(c);
      } while(accept(c, ','));
      expect(c, ')');
   }

   expect(c, '{');
   parse_prototype_body(c);
   expect(c, '}');

   push_prototype(c, nargs, &bak);

   /* turn the prototype into a closure at this point */
   emit(c, encode_ABx(OP_CLOSURE, c->pi.temp, c->pi.protos_end - 1));
   return expr_loc(c->pi.temp);
}

/*
 * Parses and compiles a table and returns an expression with the table
 * location.
 */
static struct expr
parse_table(struct compiler *c)
{
   struct expr expr;
   int32_t oldtemp;

   assert(peek(c, '{'));
   lex(c);

   expr = expr_loc(c->pi.temp);
   oldtemp = use_temp(c, &expr);

   emit(c, encode_ABx(OP_NEWTABLE, expr.val.loc, 0));

   if (!peek(c, '}')) {
      int32_t index = 0;
      bool has_key = false;

      do {
         /* parse key */
         struct expr key, value;
         int32_t oldtemp2;

         if (peek(c, tok_identifier)) {
            key = scan_id(c, c->pi.temp);
            expect(c, ':');
            has_key = true;
         }
         else {
            struct sourceloc sl = c->lex.sourceloc;
            bool square_bracket = accept(c, '[');
            key = parse_exprto(c, c->pi.temp, false);
            if (square_bracket) expect(c, ']');

            if (accept(c, ':')) {
               has_key = true;
            }
            else if (has_key) {
               error(c->lex.issue_handler, sl, "cannot leave key undefined "
                  "after table entry with explicit key.");
            }
         }

         /* key might be occupying last temporary */
         oldtemp2 = use_temp(c, &key);

         if (has_key) {
            value = parse_exprto(c, c->pi.temp, false); /* parse value */
         }
         else {
            value = key;
            key = expr_compile(c, expr_num(index++), c->pi.temp);  /* parse key */
         }

         c->pi.temp = oldtemp2;
         emit(c, encode_ABC(OP_SET, expr.val.loc, key.val.loc, value.val.loc));

      } while (accept(c, ','));
   }
   expect(c, '}');
   c->pi.temp = oldtemp;

   return expr;
}

/*
 * Parses and returns a primary expression, i.e. constants, unary expressions,
 * subexpressions, function calls.
 */
static struct expr
parse_primary_expr(struct compiler *c, struct expr_state *es)
{
   struct sourceloc sloc = c->lex.sourceloc;
   struct expr expr = expr_nil();

   switch (c->lex.tok) {
      /* literals */
      case kw_nil: lex(c); expr = expr_nil(); break;
      case kw_true: lex(c); expr = expr_bool(true); break;
      case kw_false: lex(c); expr = expr_bool(false); break;
      case tok_number: lex(c); expr = expr_num(c->lex.toknum); break;

      case tok_string:
         expr = const_fetch_str(c, c->pi.temp, c->lex.tokstr, c->lex.tokstrlen);
         lex(c);
         break;

      case '(': /* subexpression */
         expr = parse_subexpr(c, es);
         break;

      case '!': /* negation */
         lex(c);
         es->negated = !es->negated;
         expr = expr_negate(parse_primary_expr(c, es));
         es->negated = !es->negated;
         break;

      case '-': /* unary minus */
         lex(c);
         expr = expr_compile_unary(c, sloc, parse_primary_expr(c, es));
         break;

      case tok_identifier:
         expr = parse_localref_or_call(c, es);
         break;

      case kw_func:
         expr = parse_closure(c);
         break;

      case '{':
         expr = parse_table(c);
         break;

      default: error_syntax_at(c, sloc); return expr_nil();
   }

   /* parse accessors and function calls */
   for (;;) {
      bool dot_accessor = false;

      switch (c->lex.tok) {
         case '.':
         {
            lex(c);
            expr_compile(c, expr, c->pi.temp);
            int32_t temps = use_temp(c, &expr);

            /* now parse the key identifier and compile it to a register */
            check(c, tok_identifier);
            struct expr key = scan_id(c, c->pi.temp);

            c->pi.temp = temps;

            emit(c, encode_ABC(OP_GET, c->pi.temp, expr.val.loc, key.val.loc));

            expr = expr_loc(c->pi.temp);

            dot_accessor = true;

            /* skips the "dot_accessor = false" statement after the switch */
            continue;
         }

         default:
            return expr;
      }
   }

   return expr;
}

/*
 * Parses and compiles the potential right hand side of a binary expression if
 * binary operators are found. Note that the right hand side of a binary
 * expression may well be a chain of additional binary expressions. This
 * function parses the chain of binary expressions taking care of operator
 * precedence. While the precedence of the next binary operator is equal or
 * less than the currentone the function keeps parsing the expression (primary)
 * terms in a loop. As soon as a higher precedence operator is found, the
 * function recursively calls itself so that the next operator to give priority
 * to that operator.
 */
static struct expr
parse_binary_expr_rhs(struct compiler *c, struct expr_state *es,
   struct expr lhs, int32_t prec)
{
   for (;;) {
      enum binop binop; /* expr binary operator */
      int32_t tokprec;   /* token precedence */
      struct sourceloc sloc; /* operator source location */
      loc_t oldtemp; /* old temporary */
      struct expr_state rhs_es; /* rhs expr state */
      struct expr rhs; /* rhs expr */
      int32_t next_binop_prec; /* next operator token precedence */

      /* what's the lookahead operator? */
      binop = tok_to_binop(c->lex.tok);

      /* and what is it's precedence? */
      tokprec = BINOP_PRECEDENCE[binop];

      /* if the next operator precedence is lower than expression precedence
         (or next token is not an operator) then we're done. */
      if (tokprec < prec) return lhs;

      /* remember operator source location as the expression location for error
         reporting */
      sloc = c->lex.sourceloc;

      /* todo explain this */
      compile_logical_op(c, es, binop, lhs);

      lex(c); /* eat the operator token */

      /* if lhs uses the current free register, create a new state copy using
         the next register */
      oldtemp = use_temp(c, &lhs);
      rhs_es = *es;

      /* compile the right-hand-side of the binary expression */
      rhs = parse_primary_expr(c, &rhs_es);

      /* look at the new operator precedence */
      next_binop_prec = BINOP_PRECEDENCE[tok_to_binop(c->lex.tok)];

      /* if next operator precedence is higher than current expression, then
         call recursively this function to give higher priority to the next
         binary operation (pass our rhs as their lhs) */
      if (next_binop_prec > tokprec) {
         /* the target and whether the expression is currently negated are the
            same for the rhs, but reset the branch lists as rhs is a
            subexpression on its own */
         rhs_es.negated = es->negated;
         rhs_es.branch_true  = c->pi.branch_true;
         rhs_es.branch_false = c->pi.branch_false;

         /* parse the expression rhs using higher precedence than operator
            precedence */
         rhs = parse_binary_expr_rhs(c, &rhs_es, rhs, tokprec + 1);
      }

      /* sub-expr has been evaluated, restore the free register to the one
         before compiling rhs */
      c->pi.temp = oldtemp;

      /* compile the binary operation */
      lhs = expr_compile_binary(c, sloc, binop, lhs, rhs);
   }
}

/*
 * Parses and returns a subexpression, a sequence of primary expressions
 * separated by binary operators. This function takes an explicit, existing
 * expression state so that it can be called to parse sub expressions of an
 * existing expression. After calling this function, you might want to move the
 * result to a different location by calling [expr_close()].
 */
static struct expr
parse_expr(struct compiler *c, struct expr_state *es)
{
   struct expr_state my_es = *es;

   /* store the source location for error reporting */
   struct sourceloc source_loc = c->lex.sourceloc;

   /* parse expression lhs */
   struct expr lhs = parse_primary_expr(c, &my_es);

   /* if peek '=' parse the assignment `lhs = rhs` */
   if (accept(c, '=')) {

      if (!lhs.positive) goto error_lhs_not_assignable;

      switch (lhs.type) {
         case EXPR_LOCATION:
            /* check the location is a valid assignable, i.e. neither a cnst
               nor a temporary */
            if (loc_is_const(lhs.val.loc) || loc_is_temp(c, lhs.val.loc)) {
               goto error_lhs_not_assignable;
            }
            parse_exprto(c, lhs.val.loc, true);
            return lhs;

         default:
            break;
      }
   }

   return parse_binary_expr_rhs(c, &my_es, lhs, 0);

error_lhs_not_assignable:
   error(c->lex.issue_handler, source_loc,
      "lhs of assignment is not an assignable expression.");
   return expr_nil();
}

/*
 * Parses and compiles a full expression. A full expression is a subexpression
 * with a "cleared" expression state (e.g. no exiting branches, current
 * temporary as target and positive flag set to true). After the subexpression
 * is compiled, it resolves any existing branch to true or false by emitting
 * the appropriate test/testset/loadbool/jump/etc instructions.
 * The expression returned by this function is guaranteed to lie in some
 * location, whether a local or a constant.
 */
static struct expr
parse_exprto(struct compiler *c, loc_t target_hint, bool movetotarget)
{
   /* save the current number of branching instructions so that we can the
      state as we found it upon return. Also backup the first free temporary
      to be restored before returning. */
   struct expr_state es;
   es.branch_true = c->pi.branch_true;
   es.branch_false = c->pi.branch_false;
   es.negated = false;

   /* parse the subexpression with our blank state. The parsed expression might
      have generated some branching instructions to true/false. */
   struct expr expr = parse_expr(c, &es);

   /* immediately close the expression, i.e. make sure it is compiled to the
      'target' location and all open branches are closed */
   return expr_close(c, &es, expr, target_hint, movetotarget);
}

/*
 * Parses a variable declaration in the form
 *    `var <id> [= <expr>], ..., <idn> [= <exprn>]`.
 */
static void
parse_vardecl(struct compiler *c)
{
   expect(c, kw_var);
   /* parse multiple variable declarations */
   do {
      /* read the variable identifier and push it to the scope identifier
         list */
      check(c, tok_identifier);
      struct local *local = local_alloc(c, c->lex.tokstr, c->lex.tokstrlen);
      lex(c);

      /* optionally, parse the initialization expression */
      if (accept(c, '=')) {
         /* parse the expression and make sure it lies in the current
            temporary */
         parse_exprto(c, c->pi.temp, true);
      }
      else {
         /* no initialization expression provided for this variable, initialize
            it to nil */
         emit(c, encode_ABx(OP_LOADNIL, c->pi.temp, c->pi.temp));
      }

      /* define the local variable */
      local_push(c, local);

   } while (accept(c, ','));
   expect_endofstmt(c);
}


/*
 * Parses and compiles an expression only focusing on emitting appropriate
 * branching instructions to the true or false branch. The expression parsed
 * compiled is tested against [testval], i.e. if the expression is
 * [testval] (true or false) then it branches to *true*, otherwise to false.
 */
static void
parse_cond(struct compiler *c, bool testval)
{
   struct expr_state es = make_exprstate(c, !testval);

   /* parse the expression */
   struct expr expr = parse_expr(c, &es);

   /* soft-close the expression, only emit branching instructions. We don't
      care to set the final expression value to some register. Only jump to the
      two branches based on the expression result */
   if (expr_iscompare(expr.type)) {
      emit(c, encode_ABC(OP_EQ + expr.type - EXPR_EQ,
         expr.val.comp.lhs,
         expr.val.comp.rhs,
         expr.positive ^ !testval));
   }
   else {
      expr = expr_compile(c, expr, c->pi.temp);
      emit(c, encode_ABx(OP_TEST, expr.val.loc, testval));
   }

   /* emit jump to true instruction */
   *branch_push(c, &c->pi.branch_true) = c->pi.instrs_end;
   emit(c, encode_ABx(OP_JUMP, 0, 0));
}

/*
 * Parses and compiles an if statement.
 */
static void
parse_stmt_if(struct compiler *c)
{
   struct branch *branch_true_end = c->pi.branch_true;
   struct branch *branch_false_end = c->pi.branch_false;

   /* parse the condition to branch to 'true' if it's false. */
   expect(c, kw_if);
   expect(c, '(');
   parse_cond(c, false);
   expect(c, ')');

   /* bind the true branch (contained in the false branch) and parse the true
    * branch block. */
   branches_bind_here(c, &c->pi.branch_false, branch_false_end);
   parse_block(c);

   /* check if there's a else block ahead */
   if (accept(c, kw_else)) {
      /* emit the jump from the true branch that will skip the else block */
      int32_t exitjmpidx = c->pi.instrs_end;
      emit(c, OP_JUMP);

      /* bind the branch to "else branch" contained in the true branch
         (remember, we're compiling the condition to false, so branches are
         swapped). */
      branches_bind_here(c, &c->pi.branch_true, branch_true_end);

      if (peek(c, kw_if))
         parse_stmt_if(c);
      else
         parse_block(c);

      /* patch the previous jump expression */
      replace_Bx(c->instrs + exitjmpidx,
         offset_to_next_instr(c, exitjmpidx));
   }
   else {
      /* just bind the exit branch */
      branches_bind_here(c, &c->pi.branch_true, branch_true_end);
   }
}

/*
 * Parses a 'throw' statement.
 */
static void
parse_stmt_throw(struct compiler *c)
{
   expect(c, kw_throw);
   struct expr expr = parse_exprto(c, c->pi.temp, false);
   emit(c, encode_ABx(OP_THROW, 0, expr.val.loc));
   expect_endofstmt(c);
}

static void
parse_stmt_return(struct compiler *c)
{
   assert(peek(c, kw_return));
   lex(c);
   struct expr retval = parse_exprto(c, c->pi.temp, false);
   emit(c, encode_ABx(OP_RET, retval.val.loc, 1));
   expect_endofstmt(c);
}

/* TEMPORARY */
static void
parse_stmtdebug(struct compiler *c)
{
   int32_t oldtemps = c->pi.temp;

   expect(c, kw_debug);
   expect(c, '(');

   if (!peek(c, ')')) {
      do {
         parse_exprto(c, c->pi.temp, true);
         ++c->pi.temp;
      } while (accept(c, ','));
   }

   expect(c, ')');

   emit(c, encode_ABx(OP_DEBUG, oldtemps, c->pi.temp - oldtemps));
   c->pi.temp = oldtemps;
}

/*
 * Parses a single statement (e.g. a function definition, a local variable,
 * an expression, etc.)
 */
static void
parse_stmt(struct compiler *c)
{
   switch (c->lex.tok) {
      case kw_var: /* variable declaration */
         parse_vardecl(c);
         break;

      case kw_if: /* if statement */
         parse_stmt_if(c);
         break;

      case kw_debug:
         parse_stmtdebug(c);
         break;

      case kw_throw:
         parse_stmt_throw(c);
         break;

      case kw_return:
         parse_stmt_return(c);
         break;

      default: /* expression */
      {
         struct expr_state es = make_exprstate(c, false);
         parse_expr(c, &es);
         expect_endofstmt(c);
         break;
      }
   }
}

/*
 * Parses the body of a function, i.e. the statements contained in the function
 * definition.
 */
static void
parse_stmts(struct compiler *c)
{
   while (!peek(c, '}') && !peek(c, tok_eos)) {
      parse_stmt(c);
   }
}

/*
 * Parses and compiles an entire "{ stmts... }" block.
 */
static void
parse_block(struct compiler *c)
{
   expect(c, '{');

   /* remember scratch cursor before we parse the block */
   struct scratch_pos backsp = c->scratchpos;

   parse_stmts(c);

   /* pop temporary scope allocations from the scratch buffer */
   c->scratchpos = backsp;

   expect(c, '}');
}

/*
 * Parses the body of a prototype, i.e. its instructions.
 */
static void
parse_prototype_body(struct compiler *c)
{
   parse_stmts(c);

   /* emit a return nil instr if none emitted */
   if (c->pi.instrs_end > c->pi.instrs_beg &&
      decode_op(c->instrs[c->pi.instrs_end - 1]) != OP_RET)
      emit(c, encode_ABx(OP_RET, 0, 0));
}

/*
 * Parses the content of a script source file. This is nothing more but the
 * body of the main prototype followed by an end of stream.
 */
static void
parse_module(struct compiler *c)
{
   parse_prototype_body(c);
   expect(c, tok_eos);

   struct protoinfo pi = { 0 };
   push_prototype(c, 0, &pi);
}

kintern koji_result_t
compile(struct compile_info *info, struct prototype **proto)
{
   struct compiler comp = { 0 };
   struct lex_info lex_info;

   /* redirect the error handler jum\p buffer here so that we can cleanup the
      state. */
   koji_result_t result = setjmp(info->issue_handler.error_jmpbuf);
   if (result)
      goto cleanup;

   /* initialize the lex */
   lex_info.alloc = info->alloc;
   lex_info.issue_handler = &info->issue_handler;
   lex_info.source = info->source;
   lex_init(&comp.lex, &lex_info);

   /* finish setting up compiler state */
   scratch_init(&comp);
   comp.cls_string = info->cls_string;
   comp.instrs_len = 512;
   comp.instrs = kalloc(instr_t, comp.instrs_len, &info->alloc);
   comp.consts_len = 256;
   comp.consts = kalloc(union value, comp.consts_len, &info->alloc);
   comp.protos_len = 16;
   comp.protos = kalloc(struct prototype *, comp.protos_len, &info->alloc);

   /* kick off compilation! */
   parse_module(&comp);
   *proto = comp.protos[0];
   comp.pi.protos_end = 0;

cleanup:
   for (int32_t i = 0; i < comp.pi.consts_end; ++i)
      const_destroy(comp.consts[i], &info->alloc);
   for (int32_t i = 0; i < comp.pi.protos_end; ++i)
      prototype_release(comp.protos[i], &info->alloc);
   kfree(comp.instrs, comp.instrs_len, &info->alloc);
   kfree(comp.consts, comp.consts_len, &info->alloc);
   kfree(comp.protos, comp.protos_len, &info->alloc);
   scratch_deinit(&comp);
   lex_deinit(&comp.lex);
   return result;
}
