/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "kj_support.h"
#include <setjmp.h>
#include <stdarg.h>

/*
 * Describes a specific location as in line and column in a specific source file.
 */
typedef struct {
   const char *filename;
   uint line;
   uint column;
} source_location_t;

/*
 * Type of the function set by the user that will handle any compilation or runtime error.
 */
typedef void (*issue_reporter_t)(void *userdata, source_location_t, const char *message);

/*
 * Groups info about the error handler used during compilation and execution.
 */
typedef struct {
   issue_reporter_t reporter_fn;
   void *reporter_data;
   jmp_buf error_jmpbuf;
} issue_handler_t;

/*
 * Reports an issue at source location @sl with printf-like @format and arguments @args using
 * specified handler @e.
 */
void reportv(issue_handler_t *e, source_location_t sl, const char *format, va_list args);

/*
 * Reports an issue at source location @sl with printf-like @format and arguments ... using
 * specified handler @e.
 */
void report(issue_handler_t *e, source_location_t sl, const char *format, ...);

/*
 * Reports the specified error message and jumps to the error handler code.
 */
void error(issue_handler_t *e, source_location_t sl, const char *format, ...);
