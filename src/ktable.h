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

#define TABLE_DEFAULT_CAPACITY 16

/*
 * Data structure used to efficiently map keys to values, implemented with
 * a hash map.
 */
struct table {
	int32_t size;   /* number of elements in the table */
	int32_t capacity;   /* capacity of the key-value array */
	struct table_pair *pairs; /* key-value pairs */
};

/*
 * A koji table object.
 */
struct object_table
{
	struct object object;
	struct table table;
};

/*
 * Initializes the table with specified allocator and the key-value array
 * with length [capacity].
 */
kintern void
table_init(struct table*, struct koji_allocator *alloc, int32_t capacity);

/*
 * Deinitializes specified table, destroying every key and value in it and
 * deallocating the used memory.
 */
kintern void
table_deinit(struct table*, struct koji_vm*);

/*
 * Adds the mapping between [key] and [value] in specified table.
 */
kintern void
table_set(struct table*, struct koji_vm*, union value key, union value val);

/*
 * Returns the value associated to specified [key] or nil if not found.
 */
kintern union value
table_get(struct table*, struct koji_vm *vm, union value key);

/*
 * Creates a new table object and returns it in a value.
 */
kintern union value
value_new_table(struct class *cls_table, struct koji_allocator *alloc,
   int32_t capacity);

/*
 * Initializes the table class.
 */
kintern void
class_table_init(struct class *cls_table, struct class *cls_builtin);
