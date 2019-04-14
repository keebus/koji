/*
 * koji - lexical analyzer
 *
 * Copyright (C) 2019 Canio Massimo Tristano
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

#define TOKENS(_)                                                              \
	_(tok_eos, "end-of-stream")                                                 \
	_(tok_number, "number")                                                     \
	_(tok_string, "string")                                                     \
	_(tok_identifier, "identifier")                                             \
	_(kw_debug, "debug")                                                        \
	_(kw_do, "do")                                                              \
	_(kw_else, "else")                                                          \
	_(kw_false, "false")                                                        \
	_(kw_func, "func")                                                          \
	_(kw_for, "for")                                                            \
	_(kw_globals, "globals")                                                    \
	_(kw_if, "if")                                                              \
	_(kw_in, "in")                                                              \
	_(kw_nil, "nil")                                                            \
	_(kw_return, "return")                                                      \
	_(kw_this, "this")                                                          \
	_(kw_throw, "throw")                                                        \
	_(kw_true, "true")                                                          \
	_(kw_var, "var")                                                            \
	_(kw_while, "while")

#define KOJI_DEFINE_TOKEN_ENUM(enum_, _) enum_,

/*
 * The enumeration of all tokens and keywords recognized by the lexer.
 */
enum { TOKENS(KOJI_DEFINE_TOKEN_ENUM) };

#undef KOJI_DEFINE_TOKEN_ENUM

/*
 * A lexer scans a stream using a provided stream_reader matching tokens
 * recognized by the language such as the "if" keyword, or a str, or an
 * identifier. The parser then verifies that the sequence of tokens in an input
 * source file scanned by the lexer is valid and generates the appropriate
 * output bytecode. This lexer is implemented as a basic state machine that
 * implements the language tokens regular expression.
 */
struct lexer {
	/* the type of the next token in the stream (lookahead) */
	token_t tok;
	/* the next character in the stream */
	int32_t curr;
	/* used to allocate the lookahead str */
	struct koji_allocator allocator;
	/* Used to report scanning issues */
	struct issue_handler *issue_handler;
	/* input stream  */
	struct koji_source *source;
	/* loc in the input source code */
	struct sourceloc sourceloc;
	/* lookahead str */
	char *tokstr;
	/* numerical value of tok if it's a `tok_number` */
	koji_number_t toknum;
	/* lookahead str length without the null byte */
	int32_t tokstrlen;
	/* the lookahead str buffer capacity in bytes */
	int32_t tokstrbuflen;
	/* least one new-line was scanned before this token */
	bool newline;
};

struct lexer_info {
	struct koji_allocator alloc;
	struct issue_handler *issue_handler;
	struct koji_source *source;
};

/*
 * Initializes an unitialized lexer instance [l] with specified [info].
 */
kintern void
lex_init(struct lexer *, struct issue_handler *, struct koji_source *,
         struct koji_allocator *);

/*
 * De-initializes an initialized lexer instance destroying its resources.
 */
kintern void
lex_deinit(struct lexer *);

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
lex_tok_ahead_pretty_str(struct lexer *);

/*
 * Scans the next token in the source stream and returns its type.
 */
kintern token_t
lex_scan(struct lexer *);
