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
struct koji {
	struct koji_allocator alloc;
	struct koji_vm vm;
};
#endif

static void
test_string(koji_t vm)
{
   union value val = value_new_stringf(vm->class_string, &vm->alloc,
      "hello %s!", "world");
   struct string *str = value_getobjv(val);

   assert(value_isobj(val));
   assert(str->object.class == vm->class_string);
   assert(str->object.refs == 1);
   assert(str->len == 12);
   assert(strcmp(str->chars, "hello world!") == 0);

   vm_value_destroy(vm, val);
}

static void
test_table(koji_t vm)
{
   struct table t;

   table_init(&t, &vm->alloc, TABLE_DEFAULT_CAPACITY);
   table_set(&t, vm, value_num(10), value_num(118));
   table_set(&t, vm, value_num(11), value_num(119));

   union value value = table_get(&t, vm, value_num(10));
   assert(value_isnum(value) && value.num == 118.0);

	for (int32_t i = 0; i < 100; ++i)
		table_set(&t, vm, value_num(i), value_num(i * 1000));

	for (int32_t i = 0; i < 100; ++i)
		assert(table_get(&t, vm, value_num(i)).num == i * 1000);

	table_deinit(&t, vm);
}

static bool
test_simple(const char *filename)
{
   koji_t vm = koji_open(NULL);
   koji_result_t res;

   res = koji_load_file(vm, filename);
   if (res) {
      printf("Compile error: %s\n", koji_string(vm, -1));
      return false;
   }

   prototype_dump(vm->framestack[0].proto, 0, 0);

   res = koji_run(vm);
   if (res) {
      printf("Runtime error: %s\n", koji_string(vm, -1));
      return false;
   }

   koji_close(vm);
   return true;
}

#define DIR "../tests/"

static void
vector_fns(koji_t vm, enum koji_op op, int nargs)
{
   if (op == KOJI_OP_ADD) {
      nargs = 1;
   }
   else {
      /* length */
      nargs = 1;
   }
}

static bool
test_hostclass(void)
{
   koji_t vm = koji_open(NULL);

   const char *methods[] = { "length" };

   koji_push_class(vm, "Vector", methods, 1);
   koji_class_set_op(vm, -1, KOJI_OP_ADD, vector_fns);
   koji_setglobal(vm, "Vector");

   koji_result_t res = koji_load_file(vm, DIR "hostclass.kj");
   if (res) {
      printf("Compile error: %s\n", koji_string(vm, -1));
      return false;
   }

   prototype_dump(vm->framestack[0].proto, 0, 0);

   res = koji_run(vm);
   if (res) {
      printf("Runtime error: %s\n", koji_string(vm, -1));
      return false;
   }

   koji_close(vm);
   return true;
}

int32_t main()
{
   koji_t state = koji_open(NULL);

   test_string(state);
   test_table(state);

   koji_close(state);

   static const char *simple_tests[] = {
      //DIR "empty.kj",
      //DIR "assert.kj",
      //DIR "numbers.kj",
      //DIR "booleans.kj",
      //DIR "closures.kj",
      // 
      NULL
   };

   for (const char **filename = simple_tests; *filename; ++filename) {
      printf("\nRunning simple test '%s'\n", *filename);
      if (test_simple(*filename))
         printf("Success\n");
   }

   if (test_hostclass())
      printf("Success\n");

   printf("\n");
}
