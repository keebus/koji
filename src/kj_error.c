/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_error.h"
#include <stdio.h>
#include <malloc.h>

kj_intern void reportv(issue_handler_t *e, source_location_t sl, const char *format, va_list args)
{
   static const char *header_format = "at '%s' (%d:%d): ";
   uint header_length = snprintf(NULL, 0, header_format, sl.filename, sl.line, sl.column);
   uint body_length = vsnprintf(NULL, 0, format, args);
   char *message = alloca(header_length + body_length + 1);
   snprintf(message, header_length + 1, header_format, sl.filename, sl.line, sl.column);
   vsnprintf(message + header_length, header_length + body_length + 1, format, args);
   e->reporter_fn(e->reporter_data, sl, message);
}

kj_intern void report(issue_handler_t *e, source_location_t sl, const char *format, ...)
{
   va_list args;
   va_start(args, format);
   reportv(e, sl, format, args);
   va_end(args);
}

kj_intern void error(issue_handler_t *e, source_location_t sl, const char *format, ...)
{
   va_list args;
   va_start(args, format);
   reportv(e, sl, format, args);
   va_end(args);
   longjmp(e->error_jmpbuf, 0);
}
