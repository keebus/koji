/*
* koji scripting language
* 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
* This source file is part of the koji scripting language, distributed under public domain.
* See LICENSE for further licensing information.
*/

#pragma once

#include "kj_value.h"

#define TABLE_DEFAULT_CAPACITY 16

struct table {
	uint32_t size;
	uint32_t capacity;
	struct table_pair* pairs;
};

kj_intern void table_init(struct table*, struct vm*, uint32_t capacity);
kj_intern void table_deinit(struct table*, struct vm*);
kj_intern void table_set(struct table*, struct vm*, value_t key, value_t value);
kj_intern value_t table_get(struct table*, value_t key);