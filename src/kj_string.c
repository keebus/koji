/*
* koji scripting language
* 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
* This source file is part of the koji scripting language, distributed under public domain.
* See LICENSE for further licensing information.
*/

#include "kj_string.h"
#include "kj_vm.h"

kj_intern struct string* string_new(struct class* string_class, struct koji_allocator* alloc, int length)
{
	struct string* object_string = alloc->alloc(NULL, 0, sizeof(struct string) + length + 1, alloc->userdata);
	if (!object_string) return NULL; /* fixme */
	++string_class->object.references;
	object_string->object.references = 1;
	object_string->object.class = string_class;
	object_string->length = length;
	return object_string;
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


static void string_destructor(struct vm* vm, struct class* class, struct object* object)
{
	class_destructor_default(vm, class, object);
}

static union class_operator_result string_operator_add(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg1, value_t arg2)
{
	struct string* lhs_string = (struct string*)object;
	struct object* rhs_object = value_get_object(arg1);
	struct string* rhs_string = (struct string*)rhs_object;

	if (value_is_object(arg1) && rhs_object->class == class) {
		value_t result = value_new_string(class, &vm->allocator, lhs_string->length + rhs_string->length);
		struct string* result_string = (struct string*)value_get_object(result);
		memcpy(result_string->chars, lhs_string->chars, lhs_string->length);
		memcpy(result_string->chars + lhs_string->length, rhs_string->chars, rhs_string->length + 1);
		return (union class_operator_result){ result };
	}

	return class_operator_invalid(vm, class, object, op, arg1, arg2);
}

static union class_operator_result string_operator_mul(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg1, value_t arg2)
{
	struct string* lhs_string = (struct string*)object;

	if (!value_is_number(arg1)) {
		class_operator_invalid(vm, class, object, op, arg1, arg2);
		return (union class_operator_result) { value_nil() };
	}

	int32_t multiplier = (int32_t)arg1.number;
	int32_t strlength = lhs_string->length;
	value_t result = value_new_string(class, &vm->allocator, strlength * multiplier);
	struct string* result_string = (struct string*)value_get_object(result);

	int32_t offset = 0;
	for (int32_t i = 0; i < multiplier; ++i) {
		memcpy(result_string->chars + offset, lhs_string->chars, strlength);
		offset += strlength;
	}

	result_string->chars[result_string->length] = 0;
	return (union class_operator_result){ result };
}

static union class_operator_result string_operator_compare(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg1, value_t arg2)
{
	struct string* lhs_string = (struct string*)object;
	struct object* rhs_object = value_get_object(arg1);
	struct string* rhs_string = (struct string*)rhs_object;
	
	if (value_is_object(arg1) && rhs_object->class == class)
		return (union class_operator_result) {
			.int32 = lhs_string->length < rhs_string->length ? -1 :
						lhs_string->length > rhs_string->length ? 1 :
						memcmp(lhs_string->chars, rhs_string->chars, lhs_string->length)
		};

	return class_operator_compare_default(vm, class, object, op, arg1, arg2);
}

static union class_operator_result string_operator_hash(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg1, value_t arg2)
{
	struct string* lhs_string = (struct string*)object;
	return (union class_operator_result){ .uint64 = murmur2(lhs_string->chars, lhs_string->length, 0) };
}

static union class_operator_result string_operator_get(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg1, value_t arg2)
{
	struct string* lhs_string = (struct string*)object;
	return (union class_operator_result) { .value = value_number(lhs_string->chars[(size_t)arg1.number]) };
}

static union class_operator_result string_operator_set(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg1, value_t arg2)
{
	struct string* lhs_string = (struct string*)object;
	lhs_string->chars[(size_t)arg1.number] = (char)arg2.number;
}

kj_intern void class_string_init(struct class* class_string, struct class* class_class)
{
	++class_class->object.references;
	class_string->object.class = class_class;
	class_string->object.references = 1;
	class_string->name = "string";
	class_string->destructor = string_destructor;
	class_string->operator[CLASS_OPERATOR_UNM] = class_operator_invalid;
	class_string->operator[CLASS_OPERATOR_ADD] = string_operator_add;
	class_string->operator[CLASS_OPERATOR_SUB] = class_operator_invalid;
	class_string->operator[CLASS_OPERATOR_MUL] = string_operator_mul;
	class_string->operator[CLASS_OPERATOR_DIV] = class_operator_invalid;
	class_string->operator[CLASS_OPERATOR_MOD] = class_operator_invalid;
	class_string->operator[CLASS_OPERATOR_COMPARE] = string_operator_compare;
	class_string->operator[CLASS_OPERATOR_HASH] = string_operator_hash;
	class_string->operator[CLASS_OPERATOR_GET] = class_operator_invalid;
	class_string->operator[CLASS_OPERATOR_SET] = class_operator_invalid;
}
