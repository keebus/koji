/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano[gmail].com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_lexer.h"

/*
 * Skips the current character and return the next one in the stream.
 */
static int lexer_skip(struct lexer* l)
{
	if (l->curr_char == '\n') {
		++l->source_location.line;
		l->source_location.column = 0;
	}
	++l->source_location.column;
	l->curr_char = (char)l->stream_fn(l->stream_data);
	return l->curr_char;
}

/*
 * Pushes the current character to the token string and returns the next one.
 */
static int lexer_push(struct lexer* l)
{
	if (l->lookahead_string_length + 2 > l->lookahead_string_capacity) {
		l->lookahead_string = l->allocator.alloc(l->lookahead_string, l->lookahead_string_capacity, l->lookahead_string_capacity * 2, l->allocator.userdata);
		l->lookahead_string_capacity *= 2;
	}

	l->lookahead_string[l->lookahead_string_length++] = (char)l->curr_char;
	l->lookahead_string[l->lookahead_string_length] = '\0';

	return lexer_skip(l);
}

/*
 * Clears the token string to empty string.
 */
static void lexer_clear_token_string(struct lexer* l)
{
	l->lookahead_string_length = 0;
	l->lookahead_string[0] = '\0';
}

/*
 * Tries to read a whole string [str] from stream and returns whether all string was read.
 */
static int lexer_accept_str(struct lexer* l, const char* str)
{
	while (l->curr_char == *str) {
		lexer_push(l);
		++str;
	}
	return *str == 0;
}

/*
 * Accepts a character [ch] from the stream and returns if it was accepted.
 */
static int lexer_accept_char(struct lexer* l, char ch)
{
	if (l->curr_char == ch) {
		lexer_push(l);
		return true;
	}
	return false;
}

/*
 * Returns whether the next char is a valid name char.
 */
static bool lexer_is_identifier_char(int ch, bool first_char)
{
	return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch == '_') || (!first_char && ch >= '0' && ch <= '9');
}

/*
 * Scans the input for an name. \p first_char specifies whether this is the first name character
 * read. It sets l->lookahead to the tok_identifier if some name was read and returns it.
 */
static token_t lexer_scan_identifier(struct lexer* l, bool first_char)
{
	while (lexer_is_identifier_char(l->curr_char, first_char)) {
		lexer_push(l);
		l->lookahead = tok_identifier;
		first_char = false;
	}
	return l->lookahead;
}


kj_intern void lexer_init(struct lexer* l, struct lexer_info* info)
{
	l->allocator = info->allocator;
	l->issue_handler = info->issue_handler;
	l->stream_fn = info->stream_fn;
	l->stream_data = info->stream_data;
	l->lookahead_string_capacity = 128;
	l->lookahead_string = kj_alloc_type(char, l->lookahead_string_capacity, &l->allocator);
	l->lookahead_string_length = 0;
	l->source_location.filename = info->filename;
	l->source_location.line = 1;
	l->source_location.column = 0;
	l->newline = 0;
	l->curr_char = 0;

	lexer_skip(l);
	lexer_scan(l);
}

kj_intern void lexer_deinit(struct lexer* l)
{
	kj_free_type(l->lookahead_string, l->lookahead_string_capacity, &l->allocator);
}

kj_intern const char* lexer_token_to_string(token_t tok, char* buffer, int buffer_size)
{
#define TOKEN_CASE(enum_, str) case enum_ : return str;
	switch (tok) {
		TOKENS(TOKEN_CASE);
		default:
			snprintf(buffer, buffer_size, "'%s'", (const char*)&tok);
			return buffer;
	}
#undef TOKEN_CASE
}

kj_intern const char* lexer_lookahead_to_string(struct lexer* l)
{
	return (l->lookahead == tok_eos) ? "end-of-stream" : l->lookahead_string;
}

kj_intern token_t lexer_scan(struct lexer* l)
{
	l->lookahead = tok_eos;
	lexer_clear_token_string(l);

	for (;;) {
		bool decimal = false;
		switch (l->curr_char) {
			case KOJI_EOF:
				return tok_eos;

			case '\n':
				l->newline = true;

			case ' ':
			case '\r':
			case '\t':
				lexer_skip(l);
				break;

			case ',':
			case ';':
			case ':':
			case '(':
			case ')':
			case '[':
			case ']':
			case '{':
			case '}':
				l->lookahead = l->curr_char;
				lexer_push(l);
				return l->lookahead;

				/* strings  */
			case '"':
			case '\'':
			{
				int delimiter = l->curr_char;
				lexer_skip(l);
				while (l->curr_char != KOJI_EOF && l->curr_char != delimiter) {
					lexer_push(l);
				}
				if (l->curr_char != delimiter) {
					error(l->issue_handler, l->source_location,
						"end-of-stream while scanning string.");
					return l->lookahead = tok_eos;
				}
				lexer_skip(l);
				return l->lookahead = tok_string;
			}

			{
			case '.':
				decimal = true;
				lexer_push(l);
				if (l->curr_char < '0' || l->curr_char > '9')
					return l->lookahead = '.';

			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				if (!decimal) {
					/* First sequence of numbers before optional dot.  */
					while (l->curr_char >= '0' && l->curr_char <= '9') lexer_push(l);

					if (l->curr_char == '.') {
						lexer_push(l);
						decimal = true;
					}
				}

				if (decimal) {
					/* Scan decimal part  */
					while (l->curr_char >= '0' && l->curr_char <= '9') lexer_push(l);
				}
				else if (l->curr_char == 'e') {
					decimal = true;
					lexer_push(l);
					while (l->curr_char >= '0' && l->curr_char <= '9') lexer_push(l);
				}

				l->lookahead_number = (koji_number_t)atof(l->lookahead_string);
				return l->lookahead = tok_number;
			}

			case '~':
				l->lookahead = l->curr_char;
				lexer_push(l);
				return l->lookahead;

			case '!':
				lexer_push(l);
				return l->lookahead = (lexer_accept_char(l, '=') ? '!=' : '!');

			case '&':
				lexer_push(l);
				return l->lookahead = (lexer_accept_char(l, '&') ? '&&' : '&');

			case '|':
				lexer_push(l);
				return l->lookahead = (lexer_accept_char(l, '|') ? '||' : '|');

			case '=':
				lexer_push(l);
				return l->lookahead = (lexer_accept_char(l, '=') ? '==' : '=');

			case '<':
				lexer_push(l);
				return l->lookahead = (lexer_accept_char(l, '=') ? '<=' :
					lexer_accept_char(l, '<') ? '<<' : '<');

			case '>':
				lexer_push(l);
				return l->lookahead = (lexer_accept_char(l, '=') ? '>=' :
					lexer_accept_char(l, '>') ? '>>' : '>');

			case '+':
				lexer_push(l);
				return l->lookahead = (lexer_accept_char(l, '=') ? '+=' : '+');

			case '-':
				lexer_push(l);
				return l->lookahead = (lexer_accept_char(l, '=') ? '-=' : '-');

			case '*':
				lexer_push(l);
				return l->lookahead = (lexer_accept_char(l, '=') ? '*=' : '*');

			case '/':
				lexer_push(l);
				if (lexer_accept_char(l, '='))
					return l->lookahead = '/=';
				else if (l->curr_char == '/') /* line-comment  */
				{
					lexer_skip(l);
					lexer_clear_token_string(l);
					while (l->curr_char != '\n' && l->curr_char != -1) lexer_skip(l);
					break;
				}
				/* #todo add block comment  */
				return l->lookahead = '/';

				/* keywords  */
			case 'd':
				lexer_push(l);
				l->lookahead = tok_identifier;
				switch (l->curr_char) {
					case 'e':
						lexer_push(l);
						if (lexer_accept_str(l, "f")) l->lookahead = kw_def;
						break;
					case 'o':
						lexer_push(l);
						l->lookahead = kw_do;
						break;
				}
				return l->lookahead = lexer_scan_identifier(l, false);

			case 'e':
				lexer_push(l);
				l->lookahead = tok_identifier;
				if (lexer_accept_str(l, "lse")) l->lookahead = kw_else;
				return l->lookahead = lexer_scan_identifier(l, false);

			case 'f':
				lexer_push(l);
				l->lookahead = tok_identifier;
				switch (l->curr_char) {
					case 'a':
						lexer_push(l);
						if (lexer_accept_str(l, "lse")) l->lookahead = kw_false;
						break;
					case 'o':
						lexer_push(l);
						if (lexer_accept_str(l, "r")) l->lookahead = kw_for;
						break;
				}
				return l->lookahead = lexer_scan_identifier(l, false);

			case 'g':
				lexer_push(l);
				l->lookahead = tok_identifier;
				if (lexer_accept_str(l, "lobals")) l->lookahead = kw_globals;
				return l->lookahead = lexer_scan_identifier(l, false);

			case 'i':
				lexer_push(l);
				l->lookahead = tok_identifier;
				switch (l->curr_char) {
					case 'f':
						lexer_push(l);
						l->lookahead = kw_if;
						break;
					case 'n':
						lexer_push(l);
						l->lookahead = kw_in;
						break;
				}
				return l->lookahead = lexer_scan_identifier(l, false);

			case 'n':
				lexer_push(l);
				l->lookahead = tok_identifier;
				if (lexer_accept_char(l, 'i') && lexer_accept_char(l, 'l')) l->lookahead = kw_nil;
				return l->lookahead = lexer_scan_identifier(l, false);

			case 'r':
				lexer_push(l);
				l->lookahead = tok_identifier;
				if (lexer_accept_str(l, "eturn")) l->lookahead = kw_return;
				return l->lookahead = lexer_scan_identifier(l, false);

			case 't':
				lexer_push(l);
				l->lookahead = tok_identifier;
				switch (l->curr_char) {
					case 'h':
						lexer_push(l);
						if (lexer_accept_str(l, "is")) l->lookahead = kw_this;
						break;
					case 'r':
						lexer_push(l);
						if (lexer_accept_str(l, "ue")) l->lookahead = kw_true;
						break;
				}
				return l->lookahead = lexer_scan_identifier(l, false);

			case 'v':
				lexer_push(l);
				l->lookahead = tok_identifier;
				if (lexer_accept_str(l, "ar")) l->lookahead = kw_var;
				return l->lookahead = lexer_scan_identifier(l, false);

			case 'w':
				lexer_push(l);
				l->lookahead = tok_identifier;
				if (lexer_accept_str(l, "hile")) l->lookahead = kw_while;
				return l->lookahead = lexer_scan_identifier(l, false);

			default:
				lexer_scan_identifier(l, true);
				if (l->lookahead != tok_identifier) {
					error(l->issue_handler, l->source_location, "unexpected character '%c' found.", l->curr_char);
				}
				return l->lookahead;
		}
	}
}
