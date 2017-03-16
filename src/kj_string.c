/*
* koji scripting language
* 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
* This source file is part of the koji scripting language, distributed under public domain.
* See LICENSE for further licensing information.
*/

#include "kj_string.h"
#include "kj_vm.h"

static void string_destructor(struct vm* vm, struct class* class, struct object* object)
{
	/* nop */
}

static value_t string_operator_add(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg)
{
	struct string* lhs_string = (struct string*)object;
	struct object* rhs_object = value_get_object(arg);
	struct string* rhs_string = (struct string*)rhs_object;

	if (value_is_object(arg) && rhs_object->class == class) {
		value_t result = value_new_string(class, &vm->allocator, lhs_string->length + rhs_string->length);
		struct string* result_string = (struct string*)value_get_object(result);
		memcpy(result_string->chars, lhs_string->chars, lhs_string->length);
		memcpy(result_string->chars + lhs_string->length, rhs_string->chars, rhs_string->length + 1);
		return result;
	}

	vm_throw_invalid_operator(vm, class, object, op, arg);
	return value_nil();
}

static value_t string_operator_mul(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg)
{
	struct string* lhs_string = (struct string*)object;

	if (!value_is_number(arg)) {
		vm_throw_invalid_operator(vm, class, object, op, arg);
		return value_nil();
	}

	int32_t multiplier = (int32_t)arg.number;
	int32_t strlength = lhs_string->length;

	value_t result = value_new_string(class, &vm->allocator, strlength * multiplier);
	struct string* result_string = (struct string*)value_get_object(result);

	int32_t offset = 0;
	for (int32_t i = 0; i < multiplier; ++i) {
		memcpy(result_string->chars + offset, lhs_string->chars, strlength);
		offset += strlength;
	}

	result_string->chars[result_string->length] = 0;

	return result;
}

kj_intern struct string* string_new(struct class* string_class, struct koji_allocator* alloc, int length)
{
	struct string* string = alloc->alloc(NULL, 0, sizeof(struct string) + length + 1, alloc->userdata);
	if (!string) return NULL;
	++string_class->object.references;
	string->object.references = 1;
	string->object.class = string_class;
	string->length = length;
	return string;
}

kj_intern value_t value_new_string(struct class* string_class, struct koji_allocator* alloc, int length)
{
	struct string* string = string_new(string_class, alloc, length);
	if (!string) return value_nil();
	return value_object(string);
}

kj_intern value_t value_new_stringfv(struct class* string_class, struct koji_allocator* alloc, const char *format, va_list args)
{
	int length = vsnprintf(NULL, 0, format, args);
	struct string* string = string_new(string_class, alloc, length);
	if (!string) value_nil();
	vsnprintf(string->chars, length + 1, format, args);
	return value_object(string);
}

kj_intern value_t value_new_stringf(struct class* string_class, struct koji_allocator* alloc, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	value_t value = value_new_stringfv(string_class, alloc, format, args);
	va_end(args);
	return value;
}

kj_intern void class_string_init(struct class* class_string, struct class* class_class)
{
	class_string->object.class = class_class;
	class_string->object.references = 1;
	class_string->name = "string";
	class_string->destructor = string_destructor;
	class_string->operator[CLASS_OPERATOR_UNM] = vm_throw_invalid_operator;
	class_string->operator[CLASS_OPERATOR_ADD] = string_operator_add;
	class_string->operator[CLASS_OPERATOR_SUB] = vm_throw_invalid_operator;
	class_string->operator[CLASS_OPERATOR_MUL] = string_operator_mul;
	class_string->operator[CLASS_OPERATOR_DIV] = vm_throw_invalid_operator;
	class_string->operator[CLASS_OPERATOR_MOD] = vm_throw_invalid_operator;
}
