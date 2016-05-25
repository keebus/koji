/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
#include "kj_api.h"
#include "kj_value.h"
#include <math.h>
#include <stdio.h>

#ifndef NULL
#define NULL (void*)(0)
#endif

void test_values(void);
void test_string(void);

int main(int argc, char **argv)
{
   (void)argc;
   (void)argv;
   
   koji_state_t *koji = koji_open(NULL, NULL, NULL, NULL);
   koji_load_string(koji, "var a = 1 + 2; var b = 2;a = a + b");
   koji_close(koji);

   //test_values();
   //test_string();
}

void test_values(void)
{
   int arg = 0;

   value_t value = value_nil();
   assert(value_is_nil(value));
   assert(!value_is_number(value));
   assert(!value_is_object(value));

   double nan = NAN;
   value.number = nan;
   assert(!value_is_nil(value));
   assert(!value_is_boolean(value));
   assert(value_is_number(value));
   assert(!value_is_object(value));
   assert(isnan(value.number));

   value.number = 4.56693;
   assert(!value_is_nil(value));
   assert(!value_is_boolean(value));
   assert(value_is_number(value));
   assert(!value_is_object(value));
   assert(value.number == 4.56693);

   value.number = INFINITY;
   assert(!value_is_nil(value));
   assert(!value_is_boolean(value));
   assert(value_is_number(value));
   assert(!value_is_object(value));
   assert(value.number == INFINITY);

   value = value_object(&arg);
   assert(!value_is_nil(value));
   assert(!value_is_boolean(value));
   assert(!value_is_number(value));
   assert(value_is_object(value));
   assert(value_get_object(value) == &arg);

   value = value_boolean(true);
   assert(!value_is_nil(value));
   assert(value_is_boolean(value));
   assert(!value_is_number(value));
   assert(!value_is_object(value));
   assert(value_get_boolean(value) == true);

   value = value_boolean(false);
   assert(!value_is_nil(value));
   assert(value_is_boolean(value));
   assert(!value_is_number(value));
   assert(!value_is_object(value));
   assert(value_get_boolean(value) == false);
}

void test_string(void)
{
   allocator_t alloc = { NULL, default_malloc, default_realloc, default_free, };
   value_t value = value_new_string(&alloc, 20, NULL );

   value_destroy(&value, &alloc);
}
