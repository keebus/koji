/*
 * koji - opcodes and bytecode manipulation
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kstring.h"
#include "kvm.h"

#include <string.h>
#include <stdio.h>

kintern struct string *
string_new(struct class *cls_string, struct koji_allocator *alloc, int32_t len)
{
	struct string *s =
      alloc->alloc(sizeof(struct string) + len + 1, alloc->user);
	s->object.refs = 1;
	s->object.class = cls_string;
	s->len = len;
	return s;
}

kintern void
string_free(struct string *s, struct koji_allocator *alloc)
{
   alloc->free(s, sizeof(*s) + s->len + 1, alloc->user);
}

kintern union value
value_new_string(struct class *cls_string, struct koji_allocator *alloc,
   int32_t len)
{
	struct string *s = string_new(cls_string, alloc, len);
	++cls_string->object.refs;
	return value_obj(s);
}

kintern union value
value_new_stringfv(struct class *cls_string, struct koji_allocator *alloc,
   const char *format, va_list args)
{
	int32_t len = vsnprintf(NULL, 0, format, args);
	union value s = value_new_string(cls_string, alloc, len);
	vsnprintf(((struct string*)value_getobj(s))->chars, len + 1, format, args);
	return s;
}

kintern union value
value_new_stringf(struct class *cls_string, struct koji_allocator *alloc,
   const char* format, ...)
{
	va_list args;
   union value value;
	va_start(args, format);
	value = value_new_stringfv(cls_string, alloc, format, args);
	va_end(args);
	return value;
}
