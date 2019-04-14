/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kerror.h"

// TODO remove dependency on stdio.h by having custom vsnprintf
#include <stdio.h>

kintern void
reportv(struct issue_handler *e, struct sourceloc sloc, const char *format,
        va_list args)
{
	static const char *header_fmt = "at '%s' (%d:%d): ";

	int32_t header_len =
	    snprintf(NULL, 0, header_fmt, sloc.name, sloc.line, sloc.column);

	int32_t body_len = vsnprintf(NULL, 0, format, args);
	char *message = kalloca(header_len + body_len + 1);

	snprintf(message, header_len + 1, header_fmt, sloc.name, sloc.line,
	         sloc.column);

	vsnprintf(message + header_len, header_len + body_len + 1, format, args);
	e->handle(sloc, message, e->user);
}

kintern void
report(struct issue_handler *e, struct sourceloc sloc, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	reportv(e, sloc, format, args);
	va_end(args);
}

kintern void
error(struct issue_handler *e, struct sourceloc sloc, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	reportv(e, sloc, format, args);
	va_end(args);
	longjmp(e->error_jmpbuf, KOJI_ERROR_COMPILATION);
}
