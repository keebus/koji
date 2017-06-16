/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "ktable.h"
#include "kvm.h"

struct table_pair
{
	union value key;
	union value value;
};

static struct
table_pair *table_find(struct koji_vm *vm, struct table_pair *entries,
	int32_t capacity, union value key)
{
	uint64_t hash = vm_value_hash(vm, key);
	int32_t index = hash % capacity;
	/* rehash until the slot is used and the keys don't match */
	while (!value_isnil(entries[index].value) &&
			vm_value_equals(vm, entries[index].key, key))
	{
		index = (index + 1) % capacity;
	}
	return entries + index;
}

kintern void
table_init(struct table *t, struct koji_allocator *alloc, int32_t capacity)
{
	t->size = 0;
	t->capacity = capacity;
	t->pairs = kalloc(struct table_pair, capacity, alloc);
	for (int32_t i = 0; i < capacity; ++i)
	{
		t->pairs[i].key = value_nil();
		t->pairs[i].value = value_nil();
	}
}

kintern void
table_deinit(struct table *t, struct koji_vm *vm)
{
	for (int32_t i = 0; i < t->capacity; ++i)
	{
		vm_value_destroy(vm, t->pairs[i].key);
		vm_value_destroy(vm, t->pairs[i].value);
	}
	kfree(t->pairs, t->capacity, &vm->alloc);
}

kintern void
table_set(struct table *t, struct koji_vm *vm, union value key,
	union value v)
{
	struct table_pair *pair = table_find(vm, t->pairs, t->capacity, key);

	t->size += value_isnil(pair->value);

	vm_value_destroy(vm, pair->key);
	vm_value_set(vm, &pair->key, key);

	vm_value_destroy(vm, pair->value);
	vm_value_set(vm, &pair->value, v);

	/* rehash? */
	if (t->size > t->capacity * 80 / 100)
	{
		int32_t newcap = t->capacity * 2;
		struct table_pair *new_pairs = kalloc(struct table_pair,
			newcap, &vm->alloc);

		for (int32_t i = 0; i < newcap; ++i)
			new_pairs[i].key = new_pairs[i].value = value_nil();

		for (int32_t i = 0; i < t->capacity; ++i)
			if (!value_isnil(t->pairs[i].value))
				*table_find(vm, new_pairs, newcap, t->pairs[i].key)
				= t->pairs[i];

		kfree(t->pairs, t->capacity, &vm->alloc);

		t->capacity = newcap;
		t->pairs = new_pairs;
	}
}

kintern union value
table_get(struct table *table, struct koji_vm *vm, union value key)
{
	return table_find(vm, table->pairs, table->capacity, key)->value;
}

kintern union value
value_new_table(struct class *cls_table, struct koji_allocator *alloc,
	int32_t capacity)
{
	struct object_table *object_table = kalloc(struct object_table, 1, alloc);
	if (!object_table) return value_nil(); /* fixme */
	++cls_table->object.refs;
	object_table->object.refs = 1;
	object_table->object.class = cls_table;
	table_init(&object_table->table, alloc, capacity);
	return value_obj(object_table);
}
