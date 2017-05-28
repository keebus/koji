/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "koji.h"
#include "kvm.h"
#include "ktable.h"
#include "kstring.h"
#include "kbytecode.h"

#include <string.h>
#include <stdio.h>

#ifndef KOJI_AMALGAMATE
struct koji_state {
	struct koji_allocator alloc;
	struct vm vm;
};
#endif

static void
test_string(koji_state_t *state)
{
   union value val = value_new_stringf(&state->vm.cls_string, &state->vm.alloc,
      "hello %s!", "world");
   struct string *str = value_getobjv(val);

   assert(value_isobj(val));
   assert(str->object.class == &state->vm.cls_string);
   assert(str->object.refs == 1);
   assert(str->len == 12);
   assert(strcmp(str->chars, "hello world!") == 0);

   vm_value_destroy(&state->vm, val);
}

static void
test_table(koji_state_t *state)
{
   struct table t;

   table_init(&t, &state->vm.alloc, TABLE_DEFAULT_CAPACITY);
   table_set(&t, &state->vm, value_num(10), value_num(118));
   table_set(&t, &state->vm, value_num(11), value_num(119));

   union value value = table_get(&t, &state->vm, value_num(10));
   assert(value_isnum(value) && value.num == 118.0);

	for (int32_t i = 0; i < 100; ++i)
		table_set(&t, &state->vm, value_num(i), value_num(i * 1000));

	for (int32_t i = 0; i < 100; ++i)
		assert(table_get( &t, &state->vm, value_num(i)).num == i * 1000);

	table_deinit(&t, &state->vm);
}

static bool
run_simple_test(const char *filename)
{
   koji_state_t *state = koji_open(NULL);
   koji_result_t res;

   res = koji_load_file(state, filename);
   if (res) {
      printf("Compile error: %s\n", koji_string(state, -1));
      return false;
   }

   prototype_dump(state->vm.framestack[0].proto, 0);

   res = koji_run(state);
   if (res) {
      printf("Runtime error: %s\n", koji_string(state, -1));
      return false;
   }

   koji_close(state);
   return true;
}

int32_t main()
{
   koji_state_t *state = koji_open(NULL);

   test_string(state);
   test_table(state);

   koji_close(state);

#define DIR "../tests/"
   static const char *simple_tests[] = {
      DIR "empty.kj",
      DIR "numbers.kj",
      DIR "booleans.kj",
      NULL
   };

   for (const char **filename = simple_tests; *filename; ++filename) {
      printf("\nRunning simple test '%s'\n", *filename);
      if (run_simple_test(*filename))
         printf("Success\n");
   }

   printf("\n");
}
