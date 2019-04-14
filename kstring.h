/*
 * koji - opcodes and bytecode manipulation
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kclass.h"
#include "kvalue.h"

/*
 * A str object.
 */
struct string {
	/* the object base */
	struct object object;
	/* string length excluding null byte */
	int32_t len;
	/* string chars */
	char chars[];
};

#define string_getobj(value) ((struct string*)value_getobj(value))

/*
 * Allocates a str with specified length [len] using specified alloc.
 * [class_string] is a pointer to the `str` class.
 */
kintern struct string *
string_new(struct class *class_string, struct koji_allocator *, int32_t len);

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
value_new_string(struct class *class_string, struct koji_allocator *,
                 int32_t len);

/*
 * Allocates a str like [string_new] and returns an object value with it.
 */
kintern union value
value_new_stringfv(struct class *class_string, struct koji_allocator *,
                   const char *format, va_list args);

/*
 * Allocates a str like [string_new] and returns an object value with it.
 */
kintern union value
value_new_stringf(struct class *class_string, struct koji_allocator *,
                  const char *format, ...);

/*
 * Initializes class [class_string] to "str". [class_class] is the "class"
 * class.
 */
kintern void
class_string_init(struct class *class_string, struct class *cls_builtin);
