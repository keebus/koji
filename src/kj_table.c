/*
* koji scripting language
* 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
* This source file is part of the koji scripting language, distributed under public domain.
* See LICENSE for further licensing information.
*/

#include "kj_table.h"
#include "kj_vm.h"

struct table_pair {
	value_t key;
	value_t value;
};

static uint64_t table_hash(value_t value)
{
	uint64_t x = value.bits;
	x = (x ^ (x >> 30)) * (uint64_t)(0xbf58476d1ce4e5b9);
	x = (x ^ (x >> 27)) * (uint64_t)(0x94d049bb133111eb);
	x = x ^ (x >> 31);
	return x;
}

static struct table_pair* table_find(struct table_pair* entries, uint32_t capacity, value_t key)
{
	uint64_t hash = table_hash(key);
	uint32_t index = hash % capacity;
	while (!value_is_nil(entries[index].value) && entries[index].key.bits != key.bits) {
		index = (index + 1) % capacity;
	}
	return entries + index;
}

kj_intern void table_init(struct table* table, struct vm* vm, uint32_t capacity)
{
	table->size = 0;
	table->capacity = capacity;
	table->pairs = vm->allocator.alloc(NULL, 0, capacity * sizeof(struct table_pair), vm->allocator.userdata);
	for (uint32_t i = 0; i < capacity; ++i) {
		table->pairs[i].key = value_nil();
		table->pairs[i].value = value_nil();
	}
}

kj_intern void table_deinit(struct table* table, struct vm* vm)
{
	for (uint32_t i = 0; i < table->capacity; ++i) {
		table->pairs[i].key = value_nil();
		table->pairs[i].value = value_nil();
	}
	vm->allocator.alloc(table->pairs, table->capacity * sizeof(struct table_pair), 0, vm->allocator.userdata);
}

kj_intern void table_set(struct table* table, struct vm* vm, value_t key, value_t value)
{
	struct table_pair* pair = table_find(table->pairs, table->capacity, key);

	table->size += value_is_nil(pair->value);

	pair->key = key;
	pair->value = value;

	/* rehash? */
	if (table->size > table->capacity * 80 / 100) {
		uint32_t new_capacity = table->capacity * 2;
		struct table_pair* new_pairs = vm->allocator.alloc(NULL, 0, new_capacity * sizeof(struct table_pair), vm->allocator.userdata);

		for (uint32_t i = 0; i < new_capacity; ++i)
			new_pairs[i].key = new_pairs[i].value = value_nil();

		for (uint32_t i = 0; i < table->capacity; ++i)
			if (!value_is_nil(table->pairs[i].value))
				*table_find(new_pairs, new_capacity, table->pairs[i].key) = table->pairs[i];

		vm->allocator.alloc(table->pairs, table->capacity * sizeof(struct table_pair), 0, vm->allocator.userdata);

		table->capacity = new_capacity;
		table->pairs = new_pairs;
	}
}

kj_intern value_t table_get(struct table* table, value_t key)
{
	return table_find(table->pairs, table->capacity, key)->value;
}