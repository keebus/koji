/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "kj_support.h"

 /*
  * Write the #documentation.
  */
enum value_type {
   VALUE_NIL,
   VALUE_BOOL,
   VALUE_NUMBER,
   VALUE_OBJECT,
};

/*
 * Write the #documentation.
 */
typedef union value {
   double   number;
   uint64_t bits;
} value_t;

/*
 * Write the #documentation.
 */
struct object {
   int            references;
   struct class*  class;
};

struct class {
	struct object object;
	const char* name;
};

#define BITS_NAN_MASK      ((uint64_t)(0x7ff4000000000000))
#define BITS_TAG_MASK      ((uint64_t)(0xffff000000000000))
#define BITS_TAG_PAYLOAD   ~BITS_TAG_MASK
#define BITS_TAG_BOOLEAN   ((uint64_t)(0x7ffc000000000000))
#define BITS_TAG_OBJECT    ((uint64_t)(0xfffc000000000000))

/*
 * Returns a string with the type of [value].
 */
const char* value_type_str(value_t value);

/*
 * Write the #documentation.
 */
kj_forceinline value_t value_nil(void)
{
   return (value_t) { .bits = BITS_NAN_MASK };
}

/*
 * Write the #documentation.
 */
kj_forceinline value_t value_number(koji_number_t number)
{
   return (value_t) { .number = number };
}

/*
 * Write the #documentation.
 */
kj_forceinline value_t value_boolean(bool boolean)
{
   return (value_t) { .bits = BITS_TAG_BOOLEAN | boolean };
}

/*
 * Write the #documentation.
 */
kj_forceinline value_t value_object(void const *ptr)
{
   assert(((uint64_t)ptr & BITS_TAG_MASK) == 0 && "Pointer with some bit higher than 48th set.");
   return (value_t) { .bits = (BITS_TAG_OBJECT | (uint64_t)ptr) };
}

/*
 * Write the #documentation.
 */
kj_forceinline bool value_is_nil(value_t value)
{
   return value.bits == BITS_NAN_MASK;
}

/*
 * Write the #documentation.
 */
kj_forceinline bool value_is_boolean(value_t value)
{
   return (value.bits & BITS_TAG_MASK) == BITS_TAG_BOOLEAN;
}

/*
 * Write the #documentation.
 */
kj_forceinline bool value_is_number(value_t value)
{
   return (value.bits & BITS_NAN_MASK) != BITS_NAN_MASK;
}

/*
 * Write the #documentation.
 */
kj_forceinline bool value_is_object(value_t value)
{
   return (value.bits & BITS_TAG_MASK) == BITS_TAG_OBJECT;
}

/*
 * Write the #documentation.
 */
kj_forceinline bool value_get_boolean(value_t value)
{
   assert(value_is_boolean(value));
   return (value.bits & (uint64_t)1);
}

/*
 * Write the #documentation.
 */
kj_forceinline bool value_to_boolean(value_t value)
{
   if (value_is_boolean(value)) return value_get_boolean(value);
   else if (value_is_number(value)) return value.number != 0;
   else if (value_is_nil(value)) return false;
   return true;
}

/*
 * Write the #documentation.
 */
kj_forceinline struct object* value_get_object(value_t value)
{
   assert(value_is_object(value));
   return (struct object*)(intptr_t)(value.bits & BITS_TAG_PAYLOAD);
}

/*
 * Destroys [constant] (bool, number or string) using specified [allocator].
 */
kj_intern void constant_destroy(value_t constant, struct koji_allocator* allocator);

