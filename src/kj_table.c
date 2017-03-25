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

static struct table_pair* table_find(struct vm* vm, struct table_pair* entries, uint32_t capacity, value_t key)
{
	uint64_t hash = vm_value_hash(vm, key);
	uint32_t index = hash % capacity;
	while (!value_is_nil(entries[index].value) && entries[index].key.bits != key.bits) {
		index = (index + 1) % capacity;
	}
	return entries + index;
}

kj_intern void table_init(struct table* table, struct koji_allocator* allocator, uint32_t capacity)
{
	table->size = 0;
	table->capacity = capacity;
	table->pairs = allocator->alloc(NULL, 0, capacity * sizeof(struct table_pair), allocator->userdata);
	for (uint32_t i = 0; i < capacity; ++i) {
		table->pairs[i].key = value_nil();
		table->pairs[i].value = value_nil();
	}
}

kj_intern void table_deinit(struct table* table, struct vm* vm)
{
	for (uint32_t i = 0; i < table->capacity; ++i) {
		vm_value_destroy(vm, table->pairs[i].key);
		vm_value_destroy(vm, table->pairs[i].value);
	}
	vm->allocator.alloc(table->pairs, table->capacity * sizeof(struct table_pair), 0, vm->allocator.userdata);
}

kj_intern void table_set(struct table* table, struct vm* vm, value_t key, value_t value)
{
	struct table_pair* pair = table_find(vm, table->pairs, table->capacity, key);

	table->size += value_is_nil(pair->value);

	vm_value_destroy(vm, pair->key);
	vm_value_set(vm, &pair->key, key);

	vm_value_destroy(vm, pair->value);
	vm_value_set(vm, &pair->value, value);

	/* rehash? */
	if (table->size > table->capacity * 80 / 100) {
		uint32_t new_capacity = table->capacity * 2;
		struct table_pair* new_pairs = vm->allocator.alloc(NULL, 0, new_capacity * sizeof(struct table_pair), vm->allocator.userdata);

		for (uint32_t i = 0; i < new_capacity; ++i)
			new_pairs[i].key = new_pairs[i].value = value_nil();

		for (uint32_t i = 0; i < table->capacity; ++i)
			if (!value_is_nil(table->pairs[i].value))
				*table_find(vm, new_pairs, new_capacity, table->pairs[i].key) = table->pairs[i];

		vm->allocator.alloc(table->pairs, table->capacity * sizeof(struct table_pair), 0, vm->allocator.userdata);

		table->capacity = new_capacity;
		table->pairs = new_pairs;
	}
}

kj_intern value_t table_get(struct table* table, struct vm* vm, value_t key)
{
	return table_find(vm, table->pairs, table->capacity, key)->value;
}

kj_intern value_t value_new_table(struct class* class_table, struct koji_allocator* allocator, int capacity)
{
	struct object_table* object_table = kj_alloc_type(struct object_table, 1, allocator);
	if (!object_table) return value_nil(); /* fixme */
	++class_table->object.references;
	object_table->object.references = 1;
	object_table->object.class = class_table;
	table_init(&object_table->table, allocator, capacity);
	return value_object(object_table);
}

static void class_table_destructor(struct vm* vm, struct class* class, struct object* object)
{
	table_deinit(&((struct object_table*)object)->table, vm);
	class_destructor_default(vm, class, object);
}

static union class_operator_result string_operator_get(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg1, value_t arg2)
{
	struct object_table* lhs_table = (struct object_table*)object;
	return (union class_operator_result) { .value = table_get(&lhs_table->table, vm, arg1) };
}

static union class_operator_result string_operator_set(struct vm* vm, struct class* class, struct object* object, enum class_operator_kind op, value_t arg1, value_t arg2)
{
	struct object_table* lhs_table = (struct object_table*)object;
	table_set(&lhs_table->table, vm, arg1, arg2);
	return (union class_operator_result) { arg2 };
}

kj_intern void class_table_init(struct class* class_table, struct class* class_class)
{
	++class_class->object.references;
	class_init_default(class_table, class_class, "table");
	class_table->destructor = class_table_destructor;
	class_table->operator[CLASS_OPERATOR_GET] = string_operator_get;
	class_table->operator[CLASS_OPERATOR_SET] = string_operator_set;
}