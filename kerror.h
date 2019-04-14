/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Tristano
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
 * Describes a specific loc as in line and column in a specific source file.
 */
struct sourceloc {
	/* The source identifier or filename to the source file */
	const char *name;
	/* The source line where the issue is */
	int32_t line;
	/* The source column where the issue is */
	int32_t column;
};

/*
 * Groups info about the error handler used during compilation and execution.
 */
struct issue_handler {
	/* User data */
	void *user;
	/* Function pointer to the issue handler, passing the issue source location
	 * and diagnostic message */
	void (*handle)(struct sourceloc, const char *message, void *user);
	/* The location to jump to after the message has been handled */
	jmp_buf error_jmpbuf;
};

/*
 * Reports an issue at source loc @sloc with printf-like `format` and arguments
 * `args` using specified handler.
 */
kintern void
reportv(struct issue_handler *, struct sourceloc, const char *format,
        va_list args);

/*
 * Reports an issue at specified source location with printf-like `format` and
 * arguments ... using specified handler.
 */
kintern void
report(struct issue_handler *, struct sourceloc, const char *format, ...);

/*
 * Reports the specified error message and jumps to the error handler code.
 */
kintern void
error(struct issue_handler *, struct sourceloc, const char *format, ...);
