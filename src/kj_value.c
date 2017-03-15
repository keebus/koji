/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_value.h"
#include "kj_support.h"
#include "kj_string.h"
#include <stdarg.h>

const char* value_type_str(value_t value)
{
	if (value_is_nil(value)) return "nil";
	if (value_is_boolean(value)) return "bool";
	if (value_is_number(value)) return "number";
	return "object";
}

 kj_intern void constant_destroy(value_t constant, struct koji_allocator* alloc)
{
	if (value_is_object(constant)) {
		// assert value_get_object(constant)->class is string
		struct string* string = (struct string*)value_get_object(constant);
		alloc->alloc(string, sizeof(struct string) + string->length + 1, 0, alloc->userdata);
	}
}
