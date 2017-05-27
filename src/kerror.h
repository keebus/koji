/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kplatform.h"

#pragma warning(push, 0)
#include <setjmp.h>
#pragma warning(pop)

 /*
  * Describes a specific loc as in line and column in a specific source
  * file.
  */
struct sourceloc {
   const char *filename;
   int line;
   int column;
};

/*
 * Groups info about the error handler used during compilation and execution.
 */
struct issue_handler {
   void *user;
   void (*handle) (struct sourceloc, const char *message, void *user);
   jmp_buf error_jmpbuf;
};

/*
 * Reports an issue at source loc @sloc with printf-like @format and
 * arguments @args using specified handler @e.
 */
kintern void
reportv(struct issue_handler *e, struct sourceloc sloc,
   const char *format, va_list args);

/*
 * Reports an issue at source loc @sloc with printf-like @format and
 * arguments ... using specified handler @e.
 */
kintern void
report(struct issue_handler *e, struct sourceloc sloc,
   const char *format, ...);

/*
 * Reports the specified error message and jumps to the error handler code.
 */
kintern void
error(struct issue_handler *e, struct sourceloc sloc,
   const char *format, ...);
