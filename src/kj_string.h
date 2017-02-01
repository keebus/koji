/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "koji.h"
#include "kj_value.h"
#include <stdarg.h>

struct string {
   struct object object;
   int           size;
   char          chars[];
};

struct class_string {
	struct class class;
};

kj_intern inline struct string* string_new(struct class* string_class, struct koji_allocator* alloc, int length)
{
	struct string* string = alloc->alloc(NULL, 0, sizeof(struct string) + length + 1, alloc->userdata);
	if (!string) return NULL;
	//++string_class->object.references;
	string->object.references = 1;
	string->object.class = string_class;
	string->size = length;
	return string;
}

kj_intern inline value_t value_new_string(struct class* string_class, struct koji_allocator* alloc, int length)
{
	struct string* string = string_new(string_class, alloc, length);
	if (!string) return value_nil();
	return value_object(string);
}

kj_intern inline value_t value_new_stringfv(struct class* string_class, struct koji_allocator* alloc, const char *format, va_list args)
{
	int length = vsnprintf(NULL, 0, format, args);
	struct string* string = string_new(string_class, alloc, length);
	if (!string) value_nil();
	vsnprintf(string->chars, length + 1, format, args);
	return value_object(string);
}

kj_intern inline value_t value_new_stringf(struct class* string_class, struct koji_allocator* alloc, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	value_t value = value_new_stringfv(string_class, alloc, format, args);
	va_end(args);
	return value;
}