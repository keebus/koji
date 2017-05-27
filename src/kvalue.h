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

#pragma warning(push, 0)
#include <assert.h>
#pragma warning(pop)

/*
 * A value represents a generic data type. A value can be nil, have a primitive
 * value type like a num or or be a reference to an object. Koji implements
 * the value type in a compact fashion, exploiting the way the IEEE 64 bit
 * floating point type is defined. Specifically NaN values are modelled with a
 * specific mask that needs to match. The other bits not part of this mask can
 * be set to anything without making the NaN value invalid.
 */
union value {
   double num;
   u64 bits;
};

/*
 * Base of all object values, holding a reference counter and the class this
 * object belongs to.
 */
struct object {
   int            refs;
   struct class*  class;
};

#define BITS_NAN_MASK      ((u64)(0x7ff4000000000000))
#define BITS_TAG_MASK      ((u64)(0xffff000000000000))
#define BITS_TAG_PAYLOAD   ~BITS_TAG_MASK
#define BITS_TAG_BOOLEAN   ((u64)(0x7ffc000000000000))
#define BITS_TAG_OBJECT    ((u64)(0xfffc000000000000))

/*
 * Returns a str with the type of [value].
 */
kintern const char *
value_type_str(union value val);

/*
 */
kinline union value
value_nil(void)
{
   union value v;
   v.bits = BITS_NAN_MASK;
   return v;
}

/*
 */
kinline union value
value_num(koji_number_t n)
{
   union value v;
   v.num = n;
   return v;
}

/*
 */
kinline union value
value_bool(kbool b)
{
   union value v;
   v.bits = BITS_TAG_BOOLEAN | b;
   return v;
}

/*
 */
kinline union value
value_obj(void const *obj)
{
   union value v;
   assert(((u64)obj & BITS_TAG_MASK) == 0 &&
          "Pointer with some bit higher than 48th set.");
    v.bits = BITS_TAG_OBJECT | (u64)obj;
   return v;
}

/*
 */
kinline kbool
value_isnil(union value val)
{
   return val.bits == BITS_NAN_MASK;
}

/*
 */
kinline kbool
value_isbool(union value val)
{
   return (val.bits & BITS_TAG_MASK) == BITS_TAG_BOOLEAN;
}

/*
 */
kinline kbool
value_isnum(union value val)
{
   return (val.bits & BITS_NAN_MASK) != BITS_NAN_MASK;
}

/*
 */
kinline kbool
value_isobj(union value val)
{
   return (val.bits & BITS_TAG_MASK) == BITS_TAG_OBJECT;
}

/*
 */
kinline kbool
value_getbool(union value val)
{
   assert(value_isbool(val));
   return (val.bits & (u64)1);
}

/*
 */
kinline kbool
value_tobool(union value val)
{
   if (value_isbool(val)) return value_getbool(val);
   else if (value_isnum(val)) return val.num != 0;
   else if (value_isnil(val)) return kfalse;
   return ktrue;
}

/*
 */
kinline struct object *
value_getobj(union value val)
{
   assert(value_isobj(val));
   return (struct object *)(intptr_t)(val.bits & BITS_TAG_PAYLOAD);
}

#define value_getobjv(val) ((void *)value_getobj(val))