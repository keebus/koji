/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
#pragma once

#include "kj_support.h"
#include <assert.h>
#include <stdarg.h>

typedef enum {
   VALUE_NIL,
   VALUE_BOOL,
   VALUE_NUMBER,
   VALUE_OBJECT,
} value_type_t;

typedef enum {
   METHOD_TYPE_OPERATOR,
} method_type_t;

typedef union value {
   double   number;
   uint64_t bits;
} value_t;

typedef struct object {
   uint           references;
   struct class  *class;
} object_t;

struct operator {
   method_type_t type;
   value_t (*fn)(object_t *object, value_t arg);
};

typedef union {
   method_type_t type;
   struct operator operator;
} method_t;

typedef struct class {
   object_t object;
   method_t operator_neg;
   method_t operator_add;
   method_t operator_sub;
   method_t operator_mul;
   method_t operator_div;
   method_t operator_mod;
} class_t;

typedef struct {
   object_t    object;
   uint        size;
   char        chars[];
} string_t;

#define BITS_NAN_MASK      ((uint64_t)(0x7ff4000000000000))
#define BITS_TAG_MASK      ((uint64_t)(0xffff000000000000))
#define BITS_TAG_PAYLOAD   ~BITS_TAG_MASK
#define BITS_TAG_BOOLEAN   ((uint64_t)(0x7ffc000000000000))
#define BITS_TAG_OBJECT    ((uint64_t)(0xfffc000000000000))

inline value_t value_nil(void)
{
   return (value_t) { .bits = BITS_NAN_MASK };
}

inline value_t value_boolean(bool boolean)
{
   return (value_t) { .bits = BITS_TAG_BOOLEAN | boolean };
}

inline value_t value_number(koji_number_t number)
{
   return (value_t) { .number = number };
}

inline value_t value_object(void const *ptr)
{
   assert(((uint64_t)ptr & BITS_TAG_MASK) == 0 && "Pointer with some bit higher than 48th set.");
   return (value_t) { .bits = (BITS_TAG_OBJECT | (uint64_t)ptr) };
}

inline bool value_is_nil(value_t value)
{
   return value.bits == BITS_NAN_MASK;
}

inline bool value_is_boolean(value_t value)
{
   return (value.bits & BITS_TAG_MASK) == BITS_TAG_BOOLEAN;
}

inline bool value_is_number(value_t value)
{
   return (value.bits & BITS_NAN_MASK) != BITS_NAN_MASK;
}

inline  bool value_is_object(value_t value)
{
   return (value.bits & BITS_TAG_MASK) == BITS_TAG_OBJECT;
}

inline  bool value_get_boolean(value_t value)
{
   assert(value_is_boolean(value));
   return (value.bits & (uint64_t)1);
}

inline bool value_to_boolean(value_t value)
{
   if (value_is_boolean(value)) return value_get_boolean(value);
   else if (value_is_number(value)) return value.number != 0;
   else if (value_is_nil(value)) return false;
   return true;
}

inline object_t* value_get_object(value_t value)
{
   assert(value_is_object(value));
   return (void*)(intptr_t)(value.bits & BITS_TAG_PAYLOAD);
}

kj_intern value_t value_new_string(allocator_t *alloc, class_t *string_class, uint length);

kj_intern value_t value_new_stringf(allocator_t *alloc, class_t *string_class, const char *format,
                                    ...);

kj_intern value_t value_new_stringfv(allocator_t *alloc, class_t *string_class, const char *format,
                                     va_list args);

kj_intern void value_destroy(value_t *value, allocator_t *alloc);

kj_intern void value_set(value_t * dest, allocator_t * allocator, value_t * src);

inline void value_move(value_t * dest, allocator_t * allocator, value_t src)
{
   value_destroy(dest, allocator);
   *dest = src;
}

inline void value_set_nil(value_t * dest, allocator_t * allocator)
{
   value_destroy(dest, allocator);
   *dest = value_nil();
}

inline void value_set_boolean(value_t * dest, allocator_t * allocator, bool value)
{
   value_destroy(dest, allocator);
   *dest = value_boolean(value);
}

inline void value_set_number(value_t * dest, allocator_t * allocator, koji_number_t value)
{
   value_destroy(dest, allocator);
   dest->number = value;
}

kj_intern const char * value_type_str(value_t value);
