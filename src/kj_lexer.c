/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_lexer.h"
#include <stdio.h>
#include <stdlib.h>

static int skip(lexer_t *l)
{
   if (l->curr_char == '\n') {
      ++l->source_location.line;
      l->source_location.column = 0;
   }
   ++l->source_location.column;
   l->curr_char = (char)l->stream_fn(l->stream_data);
   return l->curr_char;
}

static int state_push(lexer_t *l)
{
   if (l->token_string_length + 2 > l->token_string_capacity) {
      l->token_string =
         kj_realloc(l->token_string, l->token_string_capacity * 2, 1, l->allocator);
   }

   l->token_string[l->token_string_length++] = (char)l->curr_char;
   l->token_string[l->token_string_length] = '\0';

   return skip(l);
}

static void clear_token_string(lexer_t *l)
{
   l->token_string_length = 0;
   l->token_string[0] = '\0';
}

static int accept_str(lexer_t *l, const char *str)
{
   while (l->curr_char == *str) {
      state_push(l);
      ++str;
   }
   return *str == 0;
}

static int accept_char(lexer_t *l, char ch)
{
   if (l->curr_char == ch) {
      state_push(l);
      return true;
   }
   return false;
}

/*
 * @returns whether the next char is a valid name char.
 */
static bool is_identifier_char(int ch, bool first_char)
{
   return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch == '_') ||
      (!first_char && ch >= '0' && ch <= '9');
}

/*
 * Scans the input for an name. \p first_char specifies whether this is the first name character
 * read. It sets l->lookahead to the tok_identifier if some name was read and returns it.
 */
static token_t scan_identifier(lexer_t *l, bool first_char)
{
   while (is_identifier_char(l->curr_char, first_char)) {
      state_push(l);
      l->lookahead = tok_identifier;
      first_char = false;
   }
   return l->lookahead;
}

kj_intern void lexer_init(lexer_info_t info, lexer_t *l)
{
   l->allocator = info.allocator;
   l->issue_handler = info.issue_handler;
   l->stream_fn = info.stream_fn;
   l->stream_data = info.stream_data;
   l->token_string_capacity = 16;
   l->token_string = kj_malloc(l->token_string_capacity, kj_alignof(char), l->allocator);
   l->token_string_length = 0;
   l->source_location.filename = info.filename;
   l->source_location.line = 1;
   l->source_location.column = 0;
   l->newline = 0;
   l->curr_char = 0;

   skip(l);
   lexer_scan(l);
}

kj_intern void lexer_deinit(lexer_t *l)
{
   kj_free(l->token_string, l->allocator);
}

kj_intern const char *lexer_token_to_string(token_t tok, char *buffer, uint buffer_size)
{
   switch (tok) {
      case tok_eos:           return "end-of-stream";
      case tok_number:        return "number";
      case tok_string:        return "string";
      case tok_identifier:    return "identifier";
      case kw_def:            return "def";
      case kw_do:             return "do";
      case kw_else:           return "else";
      case kw_false:          return "false";
      case kw_globals:        return "globals";
      case kw_for:            return "for";
      case kw_if:             return "if";
      case kw_in:             return "in";
      case kw_nil:            return "nil";
      case kw_return:         return "return";
      case kw_this:           return "this";
      case kw_true:           return "true";
      case kw_var:            return "var";
      case kw_while:          return "while";
      default:
         snprintf(buffer, buffer_size, "'%s'", (const char *)&tok);
         return buffer;
   }
}

kj_intern const char *lexer_lookahead_to_string(lexer_t *l)
{
   return (l->lookahead == tok_eos) ? "end-of-stream" : l->token_string;
}

kj_intern token_t lexer_scan(lexer_t *l)
{
   l->lookahead = tok_eos;
   clear_token_string(l);

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
            skip(l);
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
            state_push(l);
            return l->lookahead;

            /* strings */
         case '"':
         case '\'':
         {
            int delimiter = l->curr_char;
            skip(l);
            while (l->curr_char != KOJI_EOF && l->curr_char != delimiter) {
               state_push(l);
            }
            if (l->curr_char != delimiter) {
               error(l->issue_handler, l->source_location,
                  "end-of-stream while scanning string.");
               return l->lookahead = tok_eos;
            }
            skip(l);
            return l->lookahead = tok_string;
         }

         {
         case '.':
            decimal = true;
            state_push(l);
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
               /* First sequence of numbers before optional dot. */
               while (l->curr_char >= '0' && l->curr_char <= '9') state_push(l);

               if (l->curr_char == '.') {
                  state_push(l);
                  decimal = true;
               }
            }

            if (decimal) {
               /* Scan decimal part */
               while (l->curr_char >= '0' && l->curr_char <= '9') state_push(l);
            } else if (l->curr_char == 'e') {
               decimal = true;
               state_push(l);
               while (l->curr_char >= '0' && l->curr_char <= '9') state_push(l);
            }

            l->token_number = (koji_number)atof(l->token_string);
            return l->lookahead = tok_number;
         }

         case '~':
            l->lookahead = l->curr_char;
            state_push(l);
            return l->lookahead;

         case '!':
            state_push(l);
            return l->lookahead = (accept_char(l, '=') ? '!=' : '!');

         case '&':
            state_push(l);
            return l->lookahead = (accept_char(l, '&') ? '&&' : '&');

         case '|':
            state_push(l);
            return l->lookahead = (accept_char(l, '|') ? '||' : '|');

         case '=':
            state_push(l);
            return l->lookahead = (accept_char(l, '=') ? '==' : '=');

         case '<':
            state_push(l);
            return l->lookahead = (accept_char(l, '=') ? '<=' :
                                   accept_char(l, '<') ? '<<' : '<');

         case '>':
            state_push(l);
            return l->lookahead = (accept_char(l, '=') ? '>=' :
                                   accept_char(l, '>') ? '>>' : '>');

         case '+':
            state_push(l);
            return l->lookahead = (accept_char(l, '=') ? '+=' : '+');

         case '-':
            state_push(l);
            return l->lookahead = (accept_char(l, '=') ? '-=' : '-');

         case '*':
            state_push(l);
            return l->lookahead = (accept_char(l, '=') ? '*=' : '*');

         case '/':
            state_push(l);
            if (accept_char(l, '='))
               return l->lookahead = '/=';
            else if (l->curr_char == '/') /* line-comment */
            {
               skip(l);
               clear_token_string(l);
               while (l->curr_char != '\n' && l->curr_char != -1) skip(l);
               break;
            }
            /* todo add block comment */
            return l->lookahead = '/';

            /* keywords */
         case 'd':
            state_push(l);
            l->lookahead = tok_identifier;
            switch (l->curr_char) {
               case 'e':
                  state_push(l);
                  if (accept_str(l, "f")) l->lookahead = kw_def;
                  break;
               case 'o':
                  state_push(l);
                  l->lookahead = kw_do;
                  break;
            }
            return l->lookahead = scan_identifier(l, false);

         case 'e':
            state_push(l);
            l->lookahead = tok_identifier;
            if (accept_str(l, "lse")) l->lookahead = kw_else;
            return l->lookahead = scan_identifier(l, false);

         case 'f':
            state_push(l);
            l->lookahead = tok_identifier;
            switch (l->curr_char) {
               case 'a':
                  state_push(l);
                  if (accept_str(l, "lse")) l->lookahead = kw_false;
                  break;
               case 'o':
                  state_push(l);
                  if (accept_str(l, "r")) l->lookahead = kw_for;
                  break;
            }
            return l->lookahead = scan_identifier(l, false);

         case 'g':
            state_push(l);
            l->lookahead = tok_identifier;
            if (accept_str(l, "lobals")) l->lookahead = kw_globals;
            return l->lookahead = scan_identifier(l, false);

         case 'i':
            state_push(l);
            l->lookahead = tok_identifier;
            switch (l->curr_char) {
               case 'f':
                  state_push(l);
                  l->lookahead = kw_if;
                  break;
               case 'n':
                  state_push(l);
                  l->lookahead = kw_in;
                  break;
            }
            return l->lookahead = scan_identifier(l, false);

         case 'n':
            state_push(l);
            l->lookahead = tok_identifier;
            if (accept_char(l, 'i') && accept_char(l, 'l')) l->lookahead = kw_nil;
            return l->lookahead = scan_identifier(l, false);
            
         case 'r':
            state_push(l);
            l->lookahead = tok_identifier;
            if (accept_str(l, "eturn")) l->lookahead = kw_return;
            return l->lookahead = scan_identifier(l, false);

         case 't':
            state_push(l);
            l->lookahead = tok_identifier;
            switch (l->curr_char) {
               case 'h':
                  state_push(l);
                  if (accept_str(l, "is")) l->lookahead = kw_this;
                  break;
               case 'r':
                  state_push(l);
                  if (accept_str(l, "ue")) l->lookahead = kw_true;
                  break;
            }
            return l->lookahead = scan_identifier(l, false);

         case 'v':
            state_push(l);
            l->lookahead = tok_identifier;
            if (accept_str(l, "ar")) l->lookahead = kw_var;
            return l->lookahead = scan_identifier(l, false);

         case 'w':
            state_push(l);
            l->lookahead = tok_identifier;
            if (accept_str(l, "hile")) l->lookahead = kw_while;
            return l->lookahead = scan_identifier(l, false);

         default:
            scan_identifier(l, true);
            if (l->lookahead != tok_identifier) {
               error(l->issue_handler, l->source_location,
                  "unexpected character '%c' found.", l->curr_char);
            }
            return l->lookahead;
      }
   }
}
