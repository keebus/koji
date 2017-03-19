/*
* koji scripting language
* 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
* This source file is part of the koji scripting language, distributed under public domain.
* See LICENSE for further licensing information.
*/

#include "kj_class.h"
#include "kj_vm.h"


kj_intern union class_operator_result class_operator_invalid(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg)
{
	(void)object;

	static const char* k_operator_str[] = {
		"-", "+", "-", "*", "/", "%"
	};

	if (op == CLASS_OPERATOR_UNM) {
		vm_throw(vm, "cannot apply unary operator '%s' to '%s' object value.", k_operator_str[op], class->name);
	}
	else {
		const char* arg_type_str;
		if (value_is_object(arg)) {
			struct object* object = value_get_object(arg);
			int buffer_len = 64;
			arg_type_str = alloca(buffer_len);
			int total_len = snprintf((char*)arg_type_str, buffer_len, "'%s' object", object->class->name);
			if (total_len > buffer_len) {
				arg_type_str = alloca(total_len);
				snprintf((char*)arg_type_str, total_len, "'%s' object", object->class->name);
			}
		}
		else {
			arg_type_str = value_type_str(arg);
		}
		vm_throw(vm, "cannot apply binary operator '%s' between a %s and a %s.", k_operator_str[op], class->name, arg_type_str);
	}

	return (union class_operator_result){ 0 }; /* never executed*/
}


kj_intern union class_operator_result class_operator_compare_default(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg)
{
	struct object* rhs_object = value_get_object(arg);
	int32_t result = 1; /* objects are always greater than any other value type */

	/* if rhs is also an object, then compare the addresses */
	if (value_is_object(arg)) {
		result = (int32_t)(object - rhs_object);
	}

	return (union class_operator_result) { .int32 = result };
}

kj_intern union class_operator_result class_operator_hash_default(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg)
{
	return (union class_operator_result) { .uint64 = (uint64_t)object };
}

kj_intern void class_init_default(struct class* class, struct class* class_class, const char* name)
{
	class->object.class = class_class;
	class->object.references = 1;
	class->name = name;
	class->operator[CLASS_OPERATOR_UNM] = class_operator_invalid;
	class->operator[CLASS_OPERATOR_ADD] = class_operator_invalid;
	class->operator[CLASS_OPERATOR_SUB] = class_operator_invalid;
	class->operator[CLASS_OPERATOR_MUL] = class_operator_invalid;
	class->operator[CLASS_OPERATOR_DIV] = class_operator_invalid;
	class->operator[CLASS_OPERATOR_MOD] = class_operator_invalid;
	class->operator[CLASS_OPERATOR_COMPARE] = class_operator_compare_default;
	class->operator[CLASS_OPERATOR_HASH] = class_operator_hash_default;
}
