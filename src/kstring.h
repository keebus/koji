/*
 * koji - opcodes and bytecode manipulation
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kvalue.h"
#include "kclass.h"

/*
 * A str object.
 */
struct string {
   struct object object; /* the object base */
   int32_t len; /* str length excluding null byte */
   char chars; /* str chars */
};

/*
 * Allocates a str with specified length [len] using specified alloc.
 * [cls_string] is a pointer to the `str` class.
 */
kintern struct string *
string_new(struct class *cls_string, struct koji_allocator *, int32_t len);

/*
 * Frees string using specified allocator. This function will decrement string
 * class reference count, which must be greater than one as this function
 * will not handle string class destruction.
 */
kintern void
string_free(struct string *, struct koji_allocator *);

/*
 * Allocates a str like [string_new] and returns an object value with it.
 */
kintern union value
value_new_string(struct class *cls_string, struct koji_allocator *, int32_t len);

/*
 * Allocates a str like [string_new] and returns an object value with it.
 */
kintern union value
value_new_stringfv(struct class *cls_string, struct koji_allocator *,
   const char* format, va_list args);

/*
 * Allocates a str like [string_new] and returns an object value with it.
 */
kintern union value
value_new_stringf(struct class *cls_string, struct koji_allocator *,
   const char *format, ...);

/*
 * Initializes class [cls_string] to "str". [class_class] is the "class"
 * class.
 */
kintern void
class_string_init(struct class *cls_string, struct class *cls_builtin);
