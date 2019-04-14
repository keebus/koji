/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kmemory.h"
#include "kplatform.h"
// #include "kstate.h"

#include <stdio.h>
#include <string.h>

// static void
// test_string(koji_state_t *state)
//{
// union value val = value_new_stringf(
//    &state->vm.cls_string, &state->vm.allocator, "hello %s!", "world");
// struct string *str = value_getobjv(val);

// assert(value_isobj(val));
// assert(str->object.class == &state->vm.cls_string);
// assert(str->object.refs == 1);
// assert(str->len == 12);
// assert(strcmp(str->chars, "hello world!") == 0);

// vm_value_destroy(&state->vm, val);
//}
//
// static void
// test_table(koji_state_t *state)
//{
//	struct table t;
//
//	table_init(&t, &state->vm.allocator, TABLE_DEFAULT_CAPACITY);
//	table_set(&t, &state->vm, value_num(10), value_num(118));
//	table_set(&t, &state->vm, value_num(11), value_num(119));
//
//	union value value = table_get(&t, &state->vm, value_num(10));
//	assert(value_isnum(value) && value.num == 118.0);
//
//	for (int32_t i = 0; i < 100; ++i)
//		table_set(&t, &state->vm, value_num(i), value_num(i * 1000));
//
//	for (int32_t i = 0; i < 100; ++i)
//		assert(table_get(&t, &state->vm, value_num(i)).num == i * 1000);
//
//	table_deinit(&t, &state->vm);
//}

static void
test_buffer(void)
{
	assert(array_minlen(0) == 0);
	assert(array_minlen(1) == 64);
	assert(array_minlen(63) == 64);
	assert(array_minlen(64) == 64);
	assert(array_minlen(122) == 128);
	assert(array_minlen(128) == 128);

	uint32_t buffer_size = 0;
	double *buffer = NULL;
	for (int32_t i = 0; i < 127; ++i)
		array_seqpush(&buffer, &buffer_size, &s_default_allocator) =
		    3.14 + (double)i;

	for (int32_t i = 0; i < 127; ++i)
		assert(buffer[i] == 3.14 + (double)i);

	array_seqfree(buffer, buffer_size, &s_default_allocator);
}

static void
test_scratch(void)
{
	struct scratch_pos pos;
	struct scratch_page *s = scratch_create(&pos, &s_default_allocator);

	double *test[1000];
	for (int i = 0; i < 1000; ++i)
	{
		double* value = scratch_alloc(&pos, double, &s_default_allocator);
		*value = i + 3.14;
		test[i] = value;	
	}

	for (int i = 0; i < 1000; ++i)
		assert(*test[i] == (i + 3.14));

	pos.page = s;
	pos.curr = s->buffer;

	for (int i = 0; i < 1000; ++i)
	{
		double* value = scratch_alloc(&pos, double, &s_default_allocator);
		*value = i + 3.14;
		test[i] = value;	
	}

	for (int i = 0; i < 1000; ++i)
		assert(*test[i] == (i + 3.14));

	scratch_delete(s, &s_default_allocator);
}

static void
run_tests(void)
{
#define DIR "../tests/"
	static const char *files[] = {DIR "empty.kj"};

	for (const char **file = files; *file; ++file) {
		koji_state_t *kj = koji_create(NULL);

		if (koji_load_file(kj, *file)) {
			printf("compilation error: %s", koji_temporary_error(kj));
			koji_delete(kj);
			continue;
		}

		// todo add execution

		koji_delete(kj);
	}
}

int32_t
main()
{
	test_buffer();
	test_scratch();

	run_tests();
}
