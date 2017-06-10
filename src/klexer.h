/*
 * koji - lexical analyzer
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kerror.h"

 /*
  * Value representing an enumerated token value (e.g. kw_while) or a valid
  * sequence of characters (e.g. '>=').
  */
typedef int32_t token_t;

#define TOKENS(_)\
	_(tok_eos, "end-of-stream")\
	_(tok_number, "number")\
	_(tok_string, "string")\
	_(tok_identifier, "identifier")\
	_(kw_debug, "debug")\
	_(kw_do, "do")\
	_(kw_else, "else")\
	_(kw_false, "false")\
	_(kw_func, "func")\
	_(kw_for, "for")\
	_(kw_globals, "globals")\
	_(kw_if, "if")\
	_(kw_in, "in")\
	_(kw_nil, "nil")\
	_(kw_return, "return")\
	_(kw_this, "this")\
   _(kw_throw, "throw")\
	_(kw_true, "true")\
	_(kw_var, "var")\
	_(kw_while, "while")\

#define DEFINE_TOKEN_ENUM(enum_, _) enum_,

/*
 * The enumeration of all tokens and keywords recognized by the lexer.
 */
enum {
   TOKENS(DEFINE_TOKEN_ENUM)
};

#undef DEFINE_TOKEN_ENUM

/*
 * A lexer scans a stream using a provided stream_reader matching tokens
 * recognized by the language such as the "if" keyword, or a str, or an
 * identifier. The parser then verifies that the sequence of tokens in an input
 * source file scanned by the lexer is valid and generates the appropriate
 * output bytecode. This lexer is implemented as a basic state machine that
 * implements the language tokens regular expression.
 */
struct lex {
   token_t tok; /* the type of the next token in the stream (lookahead) */
   int32_t curr; /* the next character in the stream */
   struct koji_allocator alloc; /* used to allocate the lookahead str */
   struct issue_handler *issue_handler; /* Used to report scanning issues */
   struct koji_source *source; /* input stream  */
   struct sourceloc sourceloc; /* loc in the input source code */
   char *tokstr; /* lookahead str */
   koji_number_t toknum; /* numerical value of tok if it's a `tok_number` */
   int32_t tokstrlen; /* lookahead str length without the null byte */
   int32_t tokstrbuflen; /* the lookahead str buffer capacity in bytes */
   bool newline; /* least one new-line was scanned before this token */
};

struct lex_info {
   struct koji_allocator alloc;
   struct issue_handler *issue_handler;
   struct koji_source *source;
};

/*
 * Initializes an unitialized lexer instance [l] with specified [info].
 */
kintern void
lex_init(struct lex *l, struct lex_info *info);

/*
 * De-initializes an initialized lexer instance destroying its resources.
 */
kintern void
lex_deinit(struct lex *l);

/*
 * Converts specified token [tok] into its equivalent str and writes the
 * result in [buf] of size [bufsize].
 */
kintern const char *
lex_tok_pretty_str(token_t tok, char *buf, int32_t bufsize);

/*
 * Returns a readable str for current lookahead (e.g. it returns
 * "end-of-stream" for tok_eos)
 */
kintern const char *
lex_tok_ahead_pretty_str(struct lex *l);

/*
 * Scans the next token in the source stream and returns its type.
 */
kintern token_t
lex_scan(struct lex *l);
