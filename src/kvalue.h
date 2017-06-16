/*
 * koji - opcodes and bytecode manipulation
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kplatform.h"

/*
 * A value represents a generic data type. A value can be nil, have a primitive
 * value type like a num or or be a reference to an object. Koji implements
 * the value type in a compact fashion, exploiting the way the IEEE 64 bit
 * floating point type is defined. Specifically NaN values are modelled with a
 * specific mask that needs to match. The other bits not part of this mask can
 * be set to anything without making the NaN value invalid.
 */
union value {
   double   num;
   uint64_t bits;
};

/*
 * Base of all object values, holding a reference counter and the class this
 * object belongs to.
 */
struct object {
   int32_t refs;
   uint32_t size;
   struct class *class;
};

#define BITS_NAN_MASK      ((uint64_t)(0x7ff4000000000000))
#define BITS_TAG_MASK      ((uint64_t)(0xffff000000000000))
#define BITS_TAG_PAYLOAD   ~BITS_TAG_MASK
#define BITS_TAG_BOOLEAN   ((uint64_t)(0x7ffc000000000000))
#define BITS_TAG_OBJECT    ((uint64_t)(0xfffc000000000000))

/*
 */
static union value
value_nil(void)
{
   union value v;
   v.bits = BITS_NAN_MASK;
   return v;
}

/*
 */
static union value
value_num(koji_number_t n)
{
   union value v;
   v.num = n;
   return v;
}

/*
 */
static union value
value_bool(bool b)
{
   union value v;
   v.bits = BITS_TAG_BOOLEAN | b;
   return v;
}

/*
 */
static union value
value_obj(void const *obj)
{
   union value v;
   assert(((uint64_t)obj & BITS_TAG_MASK) == 0 &&
          "Pointer with some bit higher than 48th set.");
    v.bits = BITS_TAG_OBJECT | (uint64_t)obj;
   return v;
}

/*
 */
static bool
value_isnil(union value v)
{
   return v.bits == BITS_NAN_MASK;
}

/*
 */
static bool
value_isbool(union value v)
{
   return (v.bits & BITS_TAG_MASK) == BITS_TAG_BOOLEAN;
}

/*
 */
static bool
value_isnum(union value v)
{
   return (v.bits & BITS_NAN_MASK) != BITS_NAN_MASK;
}

/*
 */
static bool
value_isobj(union value v)
{
   return (v.bits & BITS_TAG_MASK) == BITS_TAG_OBJECT;
}

/*
 */
static bool
value_getbool(union value v)
{
   assert(value_isbool(v));
   return (v.bits & (uint64_t)1);
}

/*
 */
static bool
value_tobool(union value v)
{
   if (value_isbool(v)) return value_getbool(v);
   else if (value_isnum(v)) return v.num != 0;
   else if (value_isnil(v)) return false;
   return true;
}

/*
 * Get the object referenced by value [val].
 */
static struct object *
value_getobj(union value v)
{
   assert(value_isobj(v));
   return (struct object *)(intptr_t)(v.bits & BITS_TAG_PAYLOAD);
}

/*
 * Get the object referenced by value [val]. This macro casts the object to a
 * void*, to be used when you already know the type of the object e.g.
 * struct string *s = value_getobjv(myval);
 */
#define value_getobjv(v) ((void *)value_getobj(v))

/*
 * Returns a str with the type of [value].
 */
kintern const char *
value_type_str(union value v);

kintern void
value_const_destroy(union value c, struct koji_allocator *alloc);
