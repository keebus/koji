/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "koji.h"
#include "kj_value.h"
#include "kj_class.h"
#include <stdarg.h>

struct string {
   struct object object;
   int           length;
   char          chars[];
};

kj_intern struct string* string_new(struct class* string_class, struct koji_allocator* alloc, int length);

kj_intern value_t value_new_string(struct class* string_class, struct koji_allocator* alloc, int length);

kj_intern value_t value_new_stringfv(struct class* string_class, struct koji_allocator* alloc, const char *format, va_list args);

kj_intern value_t value_new_stringf(struct class* string_class, struct koji_allocator* alloc, const char *format, ...);

kj_intern void class_string_init(struct class* class_string, struct class* class_class);
