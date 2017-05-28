/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kclass.h"
#include "kvm.h"

#include <stdio.h>

static void
class_dtor_empty(struct vm *vm, struct object *obj)
{
   /* do nothing, this dtor should only be for class class */
}

kintern union class_op_result
class_op_invalid(struct vm *vm, struct object *obj,
   enum class_op_kind op, union value arg1, union value arg2)
{
   static const char *OPERATOR_STR[] = {
      "-", "+", "-", "*", "/", "%", "__compare", "__hash", "[]", "[]="
   };

   struct class *cls = obj->class;

	if (op == CLASS_OP_UNM) {
		vm_throw(vm, "cannot apply unary operator '%s' to '%s' object value.",
         OPERATOR_STR[op], cls->name);
	}
	else {
		const char *arg_type_str;
		if (value_isobj(arg1)) {
			struct object *argobj = value_getobj(arg1);
			int32_t buflen = 64;
			arg_type_str = kalloca(buflen);
			
         int32_t total_len = snprintf((char*)arg_type_str, buflen, "'%s' object",
            argobj->class->name);
			
         if (total_len > buflen) {
				arg_type_str = kalloca(total_len);
				snprintf((char*)arg_type_str, total_len, "'%s' object",
               argobj->class->name);
			}
		}
		else {
			arg_type_str = value_type_str(arg1);
		}
		vm_throw(vm, "cannot apply binary operator '%s' between a %s and a %s.",
         OPERATOR_STR[op], cls->name, arg_type_str);
	}

	return (union class_op_result){ 0 }; /* never executed*/
}

kintern union class_op_result
class_op_compare_default(struct vm *vm, struct object *obj,
   enum class_op_kind op, union value arg1, union value arg2)
{
	struct object *argobj = value_getobj(arg1);
   union class_op_result res = { 1 }; /* objects are always greater than any
                                         other value type */
	/* if rhs is also an object, then compare the addresses */
	if (value_isobj(arg1)) {
		res.compare = (int32_t)(obj - argobj);
	}

	return res;
}

kintern union class_op_result
class_op_hash_default(struct vm *vm, struct object *obj, enum class_op_kind op,
   union value arg1, union value arg2)
{
   union class_op_result res;
   res.hash = (uint64_t)obj;
	return res;
}

kintern void
class_init_default(struct class *cls, struct class *class_cls,
   const char *name)
{
	cls->object.class = class_cls;
	cls->object.refs = 1;
	cls->name = name;
	cls->dtor = NULL; /* must be specified */
	cls->operator[CLASS_OP_UNM] = class_op_invalid;
	cls->operator[CLASS_OP_ADD] = class_op_invalid;
	cls->operator[CLASS_OP_SUB] = class_op_invalid;
	cls->operator[CLASS_OP_MUL] = class_op_invalid;
	cls->operator[CLASS_OP_DIV] = class_op_invalid;
	cls->operator[CLASS_OP_MOD] = class_op_invalid;
	cls->operator[CLASS_OP_COMPARE] = class_op_compare_default;
	cls->operator[CLASS_OP_HASH] = class_op_hash_default;
	cls->operator[CLASS_OP_GET] = class_op_invalid;
	cls->operator[CLASS_OP_SET] = class_op_invalid;
   ++class_cls->object.refs;
}

kintern void
class_builtin_init(struct class* cls)
{
   class_init_default(cls, cls, "builtin_class");
   cls->dtor = class_dtor_empty;
}
