/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano[gmail].com>
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
 * The enumeration of all tokens and keywords recognized by the lexer.
 */
enum
{
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
typedef struct
{
   /* The next character in the stream. */
   int curr_char;

   /* Allocator used to allocate the lookahead string. */
   allocator_t* allocator;

   /* Used to report scanning issues. */
   issue_handler_t* issue_handler;

   /* Input stream data. */
   void* stream_data;

   /* Input stream function. */
   kj_stream_read_t stream_fn;

   /* Identifies a location in the input source code. */
   source_location_t source_location;

   /* The type of the next token in the stream (called lookahead). */
   token_t lookahead;

   /* Whether at least one new-line was scanned before this token. */
   bool newline;

   /* Lookahead string. */
   char* lookahead_string;

   /* Lookahead string size (number of characters excluded the null byte). */
   uint lookahead_string_length;

   /* The lookahead string buffer capacity in bytes. */
   uint lookahead_string_capacity;

   /* Iff the lookahead is a `tok_number` this variable holds its numerical value. */
   kj_number_t lookahead_number;
} lexer_t;

/*
 * Write the #documentation.
 */
typedef struct
{
   /* Write the #documentation. */
   allocator_t* allocator;

   /* Write the #documentation. */
   issue_handler_t* issue_handler;

   /* Write the #documentation. */
   const char* filename;

   /* Write the #documentation. */
   kj_stream_read_t stream_fn;

   /* Write the #documentation. */
   void* stream_data;
} lexer_info_t;

/*
 * Initializes an unitialized lexer instance [l] to use specified error handler [e]. Lexer will scan
 * source using specified stream [stream_func] and [stream_data], using [filename] as the source
 * origin descriptor.
 */
kj_intern void lexer_init(lexer_info_t* info, lexer_t* l);

/*
 * De-initializes an initialized lexer instance destroying its resources.
 */
kj_intern void lexer_deinit(lexer_t* l);

/*
 * Converts specified token [tok] into its equivalent string and writs the result in [buffer] of size
 * [buffer_size].
 */
kj_intern const char* lexer_token_to_string(token_t tok, char* buffer, uint buffer_size);

/*
 * Returns a readable string for current lookahead (e.g. it returns "end-of-stream" for tok_eos)
 */
kj_intern const char* lexer_lookahead_to_string(lexer_t* l);

/*
 * Scans the next token in the source stream and returns its type.
 */
kj_intern token_t lexer_scan(lexer_t* l);
