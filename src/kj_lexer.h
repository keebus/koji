/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "kj_error.h"

/*
 * Value representing an enumerated token value (e.g. kw_while) or a valid sequence of characters
 * (e.g. '>=').
 */
typedef int token_t;

/*
 * Write the #documentation.
 */
enum {
   /* tokens */
   tok_eos = -127,
   tok_number,
   tok_string,
   tok_identifier,

   /* keywords */
   kw_def,
   kw_do,
   kw_else,
   kw_false,
   kw_globals,
   kw_for,
   kw_if,
   kw_in,
   kw_nil,
   kw_return,
   kw_this,
   kw_true,
   kw_var,
   kw_while,
};

/*
 * A lexer scans a stream using a provided stream_reader matching tokens recognized by the language
 * such as the "if" keyword, or a string, or an identifier. The parser then verifies that the
 * sequence of tokens in an input source file scanned by the lexer is valid and generates the
 * appropriate output bytecode. This lexer is implemented as a basic state machine that implements
 * the language tokens regular expression.
 */
typedef struct {
   int curr_char;
   allocator_t *allocator;
   issue_handler_t *issue_handler;
   void *stream_data;
   koji_stream_read_t stream_fn;
   source_location_t source_location;
   token_t lookahead;
   bool newline;
   char *token_string;
   uint token_string_length;
   uint token_string_capacity;
   koji_number token_number;
} lexer_t;

/*
 * Write the #documentation.
 */
typedef struct {
   allocator_t *allocator;
   issue_handler_t *issue_handler;
   const char *filename;
   koji_stream_read_t stream_fn;
   void *stream_data;
} lexer_info_t;

/*
 * Initializes an unitialized lexer instance @l to use specified error handler @e. Lexer will scan
 * source using specified stream @stream_func and @stream_data, using @filename as the source
 * origin descriptor.
 */
kj_intern void lexer_init(lexer_info_t info, lexer_t *l);

/*
 * De-initializes an initialized lexer instance destroying its resources.
 */
kj_intern void lexer_deinit(lexer_t *l);

/*
 * Converts specified token @tok into its equivalent string and writs the result
 * in @buffer of size
 * @buffer_size.
 */
kj_intern const char *lexer_token_to_string(token_t tok, char *buffer, uint buffer_size);

/*
 * @returns a readable string for current lookahead (e.g. it returns
 * "end-of-stream" for tok_eos)
 */
kj_intern const char *lexer_lookahead_to_string(lexer_t *l);

/*
 * Scans the next token in the source stream and returns its type.
 */
kj_intern token_t lexer_scan(lexer_t *l);
