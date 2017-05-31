/*
 * koji - lexical analyzer
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "klexer.h"
#include "kio.h"

#pragma warning(push, 0)
#include <stdio.h>
#include <stdlib.h>
#pragma warning(pop)

/*
 * Skips the current character and return the next one in the stream.
 */
static int32_t
lex_skip(struct lex *l)
{
	if (l->curr == '\n') {
		++l->sourceloc.line;
		l->sourceloc.column = 0;
	}
	++l->sourceloc.column;
	l->curr = (char)l->source->fn(l->source->user);
	return l->curr;
}

/*
 * Pushes the current character to the token str and returns the next one.
 */
static int32_t
lex_push(struct lex *l)
{
	if (l->tokstrlen + 2 > l->tokstrbuflen) {
		l->tokstr = l->alloc.realloc(l->tokstr, l->tokstrbuflen,
         l->tokstrbuflen * 2, l->alloc.user);
		l->tokstrbuflen *= 2;
	}

	l->tokstr[l->tokstrlen++] = (char)l->curr;
	l->tokstr[l->tokstrlen] = '\0';

	return lex_skip(l);
}

/*
 * Clears the token str to empty str.
 */
static void
lex_tokstr_clear(struct lex *l)
{
	l->tokstrlen = 0;
	l->tokstr[0] = '\0';
}

/*
 * Tries to read a whole str [str] from stream and returns whether all
 * str was read.
 */
static int32_t
lex_accept_str(struct lex *l, const char *str)
{
	while (l->curr == *str) {
		lex_push(l);
		++str;
	}
	return *str == 0;
}

/*
 * Accepts a character [ch] from the stream and returns if it was accepted.
 */
static int32_t
lex_accept_char(struct lex *l, char ch)
{
	if (l->curr == ch) {
		lex_push(l);
		return true;
	}
	return false;
}

/*
 * Returns whether the next char is a valid name char.
 */
static bool
lex_is_id_char(int32_t ch, bool first_char)
{
	return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
      (ch == '_') || (!first_char && ch >= '0' && ch <= '9');
}

/*
 * Scans the input for an name. \p first_char specifies whether this is the
 * first name character read. It sets l->lookahead to the tok_identifier if
 * some name was read and returns it.
 */
static token_t
lex_scan_id(struct lex *l, bool first_char)
{
	while (lex_is_id_char(l->curr, first_char)) {
		lex_push(l);
		l->tok = tok_identifier;
		first_char = false;
	}
	return l->tok;
}


kintern void
lex_init(struct lex *l, struct lex_info *info)
{
	l->alloc = info->alloc;
	l->issue_handler = info->issue_handler;
	l->source = info->source;
	l->tokstrbuflen = 128;
	l->tokstr = kalloc(char, l->tokstrbuflen, &l->alloc);
	l->tokstrlen = 0;
	l->sourceloc.filename = info->filename;
	l->sourceloc.line = 1;
	l->sourceloc.column = 0;
	l->newline = 0;
	l->curr = 0;

	lex_skip(l);
	lex_scan(l);
}

kintern void
lex_deinit(struct lex *l)
{
	kfree(l->tokstr, l->tokstrbuflen, &l->alloc);
}

kintern const char *
lex_tok_pretty_str(token_t tok, char *buffer, int32_t buffer_size)
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

kintern const char *
lex_tok_ahead_pretty_str(struct lex *l)
{
	return (l->tok == tok_eos) ? "end-of-stream" : l->tokstr;
}

kintern token_t
lex_scan(struct lex *l)
{
	l->tok = tok_eos;
	lex_tokstr_clear(l);

	for (;;) {
		bool decimal = false;
		switch (l->curr) {
			case KOJI_EOF:
				return tok_eos;

			case '\n':
				l->newline = true;

			case ' ':
			case '\r':
			case '\t':
				lex_skip(l);
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
				l->tok = l->curr;
				lex_push(l);
				return l->tok;

				/* strings  */
			case '"':
			case '\'':
			{
				int32_t delimiter = l->curr;
				lex_skip(l);
				while (l->curr != KOJI_EOF && l->curr != delimiter) {
					lex_push(l);
				}
				if (l->curr != delimiter) {
					error(l->issue_handler, l->sourceloc,
						"end-of-stream while scanning string.");
					return l->tok = tok_eos;
				}
				lex_skip(l);
				return l->tok = tok_string;
			}

			{
			case '.':
				decimal = true;
				lex_push(l);
				if (l->curr < '0' || l->curr > '9')
					return l->tok = '.';

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
					while (l->curr >= '0' && l->curr <= '9')
                  lex_push(l);

					if (l->curr == '.') {
						lex_push(l);
						decimal = true;
					}
				}

				if (decimal) {
					/* Scan decimal part  */
					while (l->curr >= '0' && l->curr <= '9')
                  lex_push(l);
				}
				else if (l->curr == 'e') {
					decimal = true;
					lex_push(l);
					while (l->curr >= '0' && l->curr <= '9')
                  lex_push(l);
				}

				l->toknum = (koji_number_t)atof(l->tokstr);
				return l->tok = tok_number;
			}

			case '~':
				l->tok = l->curr;
				lex_push(l);
				return l->tok;

			case '!':
				lex_push(l);
				return l->tok = (lex_accept_char(l, '=') ? '!=' : '!');

			case '&':
				lex_push(l);
				return l->tok = (lex_accept_char(l, '&') ? '&&' : '&');

			case '|':
				lex_push(l);
				return l->tok = (lex_accept_char(l, '|') ? '||' : '|');

			case '=':
				lex_push(l);
				return l->tok = (lex_accept_char(l, '=') ? '==' : '=');

			case '<':
				lex_push(l);
				return l->tok = (lex_accept_char(l, '=') ? '<=' :
					lex_accept_char(l, '<') ? '<<' : '<');

			case '>':
				lex_push(l);
				return l->tok = (lex_accept_char(l, '=') ? '>=' :
					lex_accept_char(l, '>') ? '>>' : '>');

			case '+':
				lex_push(l);
				return l->tok = (lex_accept_char(l, '=') ? '+=' : '+');

			case '-':
				lex_push(l);
				return l->tok = (lex_accept_char(l, '=') ? '-=' : '-');

			case '*':
				lex_push(l);
				return l->tok = (lex_accept_char(l, '=') ? '*=' : '*');

			case '/':
				lex_push(l);
				if (lex_accept_char(l, '='))
					return l->tok = '/=';
				else if (l->curr == '/') /* line-comment  */
				{
					lex_skip(l);
					lex_tokstr_clear(l);
					while (l->curr != '\n' && l->curr != -1)
                  lex_skip(l);
					break;
            }
            else if (l->curr == '*') {
               lex_tokstr_clear(l);
               do {
                  lex_skip(l);
                  while (l->curr != '*' && l->curr != -1)
                     lex_skip(l);
                  if (l->curr == -1) {
                     error(l->issue_handler, l->sourceloc,
                        "end-of-stream found while scanning comment block.");
                     return 0; /* unreachable */
                  }
                  lex_skip(l);
               } while (l->curr != '/');
               lex_skip(l);
					break;
            }
				return l->tok = '/';

				/* keywords  */
			case 'd':
				lex_push(l);
				l->tok = tok_identifier;
				switch (l->curr) {
					case 'e':
						lex_push(l);
						if (lex_accept_str(l, "bug"))
							l->tok = kw_debug;
						break;
					case 'o':
						lex_push(l);
						l->tok = kw_do;
						break;
				}
				return l->tok = lex_scan_id(l, false);

			case 'e':
				lex_push(l);
				l->tok = tok_identifier;
				if (lex_accept_str(l, "lse"))
               l->tok = kw_else;
				return l->tok = lex_scan_id(l, false);

			case 'f':
				lex_push(l);
				l->tok = tok_identifier;
				switch (l->curr) {
					case 'a':
						lex_push(l);
						if (lex_accept_str(l, "lse"))
                     l->tok = kw_false;
						break;
					case 'o':
						lex_push(l);
						if (lex_accept_str(l, "r"))
                     l->tok = kw_for;
						break;
               case 'u':
						lex_push(l);
						if (lex_accept_str(l, "nc"))
                     l->tok = kw_func;
						break;
				}
				return l->tok = lex_scan_id(l, false);

			case 'g':
				lex_push(l);
				l->tok = tok_identifier;
				if (lex_accept_str(l, "lobals"))
               l->tok = kw_globals;
				return l->tok = lex_scan_id(l, false);

			case 'i':
				lex_push(l);
				l->tok = tok_identifier;
				switch (l->curr) {
					case 'f':
						lex_push(l);
						l->tok = kw_if;
						break;
					case 'n':
						lex_push(l);
						l->tok = kw_in;
						break;
				}
				return l->tok = lex_scan_id(l, false);

			case 'n':
				lex_push(l);
				l->tok = tok_identifier;
				if (lex_accept_char(l, 'i') && lex_accept_char(l, 'l'))
               l->tok = kw_nil;
				return l->tok = lex_scan_id(l, false);

			case 'r':
				lex_push(l);
				l->tok = tok_identifier;
				if (lex_accept_str(l, "eturn"))
               l->tok = kw_return;
				return l->tok = lex_scan_id(l, false);

			case 't':
				lex_push(l);
				l->tok = tok_identifier;
				switch (l->curr) {
					case 'h':
						lex_push(l);
                  if (l->curr == 'r') {
                     lex_push(l);
                     if (lex_accept_str(l, "ow")) {
                        l->tok = kw_throw;
                     }
                  }
						else if (lex_accept_str(l, "is"))
                     l->tok = kw_this;
						break;
					case 'r':
						lex_push(l);                  
						if (lex_accept_str(l, "ue"))
                     l->tok = kw_true;
						break;
				}
				return l->tok = lex_scan_id(l, false);

			case 'v':
				lex_push(l);
				l->tok = tok_identifier;
				if (lex_accept_str(l, "ar"))
               l->tok = kw_var;
				return l->tok = lex_scan_id(l, false);

			case 'w':
				lex_push(l);
				l->tok = tok_identifier;
				if (lex_accept_str(l, "hile"))
               l->tok = kw_while;
				return l->tok = lex_scan_id(l, false);

			default:
				lex_scan_id(l, true);
				if (l->tok != tok_identifier) {
					error(l->issue_handler, l->sourceloc,
                  "unexpected character '%c' found.", l->curr);
				}
				return l->tok;
		}
	}
}
