/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
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
struct source_location {
	const char *filename;
	int line;
	int column;
};

/*
 * Groups info about the error handler used during compilation and execution.
 */
struct issue_handler {
	void* userdata;
	void(*handle) (struct source_location, const char *message, void *userdata);
	jmp_buf error_jmpbuf;
};

/*
 * Reports an issue at source location @sl with printf-like @format and arguments @args using
 * specified handler @e.
 */
kj_intern void reportv(struct issue_handler *e, struct source_location sl, const char *format, va_list args);

/*
 * Reports an issue at source location @sl with printf-like @format and arguments ... using
 * specified handler @e.
 */
kj_intern void report(struct issue_handler *e, struct source_location sl, const char *format, ...);

/*
 * Reports the specified error message and jumps to the error handler code.
 */
kj_intern void error(struct issue_handler *e, struct source_location sl, const char *format, ...);
