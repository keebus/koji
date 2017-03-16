/*
* koji scripting language
* 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
* This source file is part of the koji scripting language, distributed under public domain.
* See LICENSE for further licensing information.
*/

#include "kj_class.h"
#include "kj_vm.h"

kj_intern value_t vm_throw_invalid_operator(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg)
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

	return value_nil(); /* never executed*/
}
