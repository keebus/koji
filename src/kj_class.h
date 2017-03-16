/*
* koji scripting language
* 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
* This source file is part of the koji scripting language, distributed under public domain.
* See LICENSE for further licensing information.
*/

#pragma once

#include "kj_value.h"

enum class_operator_kind {
	CLASS_OPERATOR_UNM,
	CLASS_OPERATOR_ADD,
	CLASS_OPERATOR_SUB,
	CLASS_OPERATOR_MUL,
	CLASS_OPERATOR_DIV,
	CLASS_OPERATOR_MOD,
	CLASS_OPERATOR_COUNT_,
};

struct class {
	struct object object;
	const char* name;
	void (*destructor)(struct vm*, struct class*, struct object*);
	value_t (*operator[CLASS_OPERATOR_COUNT_])(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg);
};

kj_intern value_t vm_throw_invalid_operator(struct vm*, struct class*, struct object* object, enum class_operator_kind, value_t arg);
