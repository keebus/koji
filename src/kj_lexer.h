/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "koji.h"
#include "kj_error.h"

 /*
  * Value representing an enumerated token value (e.g. kw_while) or a valid sequence of characters
  * (e.g. '>=').
  */
typedef int token_t;

#define TOKENS(_)\
	_(tok_eos, "end-of-stream")\
	_(tok_number, "number")\
	_(tok_string, "string")\
	_(tok_identifier, "identifier")\
	_(kw_debug, "debug")\
	_(kw_def, "def")\
	_(kw_do, "do")\
	_(kw_else, "else")\
	_(kw_false, "false")\
	_(kw_for, "for")\
	_(kw_globals, "globals")\
	_(kw_if, "if")\
	_(kw_in, "in")\
	_(kw_nil, "nil")\
	_(kw_return, "return")\
	_(kw_this, "this")\
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
 * A lexer scans a stream using a provided stream_reader matching tokens recognized by the language
 * such as the "if" keyword, or a string, or an identifier. The parser then verifies that the
 * sequence of tokens in an input source file scanned by the lexer is valid and generates the
 * appropriate output bytecode. This lexer is implemented as a basic state machine that implements
 * the language tokens regular expression.
 */
struct lexer {
	/* The next character in the stream. */
	int curr_char;

	/* Allocator used to allocate the lookahead string. */
	struct koji_allocator allocator;

	/* Used to report scanning issues. */
	struct issue_handler* issue_handler;

	/* Input stream data. */
	void* stream_data;

	/* Input stream function. */
	koji_stream_read_t stream_fn;

	/* Identifies a location in the input source code. */
	struct source_location source_location;

	/* The type of the next token in the stream (called lookahead). */
	token_t lookahead;

	/* Whether at least one new-line was scanned before this token. */
	bool newline;

	/* Lookahead string. */
	char* lookahead_string;

	/* Lookahead string size (number of characters excluded the null byte). */
	int lookahead_string_length;

	/* The lookahead string buffer capacity in bytes. */
	int lookahead_string_capacity;

	/* Iff the lookahead is a `tok_number` this variable holds its numerical value. */
	koji_number_t lookahead_number;
};

/*
 * Write the #documentation.
 */
struct lexer_info {
	/* Write the #documentation. */
	struct koji_allocator allocator;

	/* Write the #documentation. */
	struct issue_handler* issue_handler;

	/* Write the #documentation. */
	const char* filename;

	/* Write the #documentation. */
	koji_stream_read_t stream_fn;

	/* Write the #documentation. */
	void* stream_data;
};

/*
 * Initializes an unitialized lexer instance [l] to use specified error handler [e]. Lexer will scan
 * source using specified stream [stream_func] and [stream_data], using [filename] as the source
 * origin descriptor.
 */
kj_intern void lexer_init(struct lexer* l, struct lexer_info* info);

/*
 * De-initializes an initialized lexer instance destroying its resources.
 */
kj_intern void lexer_deinit(struct lexer* l);

/*
 * Converts specified token [tok] into its equivalent string and writs the result in [buffer] of size
 * [buffer_size].
 */
kj_intern const char* lexer_token_to_string(token_t tok, char* buffer, int buffer_size);

/*
 * Returns a readable string for current lookahead (e.g. it returns "end-of-stream" for tok_eos)
 */
kj_intern const char* lexer_lookahead_to_string(struct lexer* l);

/*
 * Scans the next token in the source stream and returns its type.
 */
kj_intern token_t lexer_scan(struct lexer* l);
