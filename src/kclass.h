/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kvalue.h"

struct vm;

/*
 * Supported class operator enum.
 */
enum class_op_kind {
	CLASS_OP_UNM,
	CLASS_OP_ADD,
	CLASS_OP_SUB,
	CLASS_OP_MUL,
	CLASS_OP_DIV,
	CLASS_OP_MOD,
	CLASS_OP_COMPARE,
	CLASS_OP_HASH,
	CLASS_OP_GET,
	CLASS_OP_SET,
	CLASS_OP_COUNT_,
};

/*
 * Type returned by class operators. Some operators return a [value] type while
 * others like [hash] return an uint64. The reason we use this other "value"
 * type and not just value is for speed, so that we don't do any unnecessary
 * double <-> integer conversion.
 */
union class_op_result {
	union value value; /* class operator value */
	uint64_t hash;          /* hash value returned by the hash op */
	int32_t compare;       /* comparison value returned by the compare op [-1, +1]*/
};

/*
 * Class destructor function type. Takes an object and destructs it, i.e. it
 * releases all other resources held by the object. This function is also
 * responsible for deallocating the object itself.
 */
typedef void (*class_dtor_t) (struct vm*, struct object*);

/*
 * Class operator function type. [vm] is the current virtual machine invoking
 * this operator. [obj] is the object the operator is invoked on, [op] is the
 * operator kind to be invoked while [arg1] and [arg2] are the arguments to the
 * operator (arg2 might be nil, depending on the operator kind)
 */
typedef union class_op_result (*class_op_t) (struct vm *vm, struct object *obj,
   enum class_op_kind op, union value arg1, union value arg2);

/*
 * The class object base type. Every object in koji is an instance of a class,
 * which in turn is an object itself of class "class". A class has pointers to
 * its destructor, its operators (if any) and the table of member functions.
 */
struct class {
	struct object object;
	const char *name;
	class_dtor_t dtor;
	class_op_t operator[CLASS_OP_COUNT_];
};

/*
 * Invalid class operator. Classes that don't support a specific operator
 * should bind it to this function. It simply reports a runtime error.
 */
kintern union class_op_result
class_op_invalid(struct vm*, struct object *, enum class_op_kind,
   union value arg1, union value arg2);

/*
 * Class default compare operator. It sorts by type first, with primitive types
 * coming first and if both are objects, it sorts by class address first and
 * by object address if they belong to the same class.
 */
kintern union class_op_result
class_op_compare_default(struct vm*, struct object *,
   enum class_op_kind, union value arg1, union value arg2);

/*
 * Class default hash operator. It hashes the object pointer.
 */
kintern union class_op_result
class_op_hash_default(struct vm*, struct object *, enum class_op_kind,
   union value arg1, union value arg2);

/*
 * Initializes the class [cls] to a default class where [class_cls] is the 
 * "class" class and [name] is the class [cls] name.
 */
kintern void
class_init_default(struct class* cls, struct class *class_cls,
   const char *name);

/*
 * Initializes the "class" class.
 */
kintern void
class_builtin_init(struct class* cls);
