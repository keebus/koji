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
	const int32_t size = sizeof(struct string) + len + 1;
	struct string *s = alloc->alloc(size, alloc->user);
	s->object.refs = 1;
	s->object.size = size;
	s->object.class = cls_string;
	return s;
}

kintern void
string_free(struct string *s, struct koji_allocator *alloc)
{
   alloc->free(s, s->object.size, alloc->user);
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

static uint64_t
string_class_hash(struct string *str)
{
	return murmur2(str->chars, string_len(str), 0);
}

kintern struct class *
class_string_new(struct class *class_class, struct koji_allocator *alloc)
{
	struct class* c = class_new(class_class, "string", 6, NULL, 0, alloc);
	c->hash = (class_op_hash_t)string_class_hash;
	return c;
}
