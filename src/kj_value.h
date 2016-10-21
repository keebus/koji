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

 /*
  * Write the #documentation.
  */
typedef enum
{
   VALUE_NIL,
   VALUE_BOOL,
   VALUE_NUMBER,
   VALUE_OBJECT,
} value_type_t;

/*
 * Write the #documentation.
 */
typedef union value
{
   double   number;
   uint64_t bits;
} value_t;

/*
 * Write the #documentation.
 */
typedef struct object
{
   uint           references;
   struct klass  *klass;
} object_t;

/*
 * Write the #documentation.
 */
typedef enum
{
   CLASS_TYPE_BUILTIN,
} klass_type_t;

typedef enum
{
   KLASS_OPERATOR_ADD,
   KLASS_OPERATOR_SUB,
   KLASS_OPERATOR_MUL,
   KLASS_OPERATOR_DIV,
   KLASS_OPERATOR_MOD,
   KLASS_OPERATOR_NEG,
   KLASS_OPERATOR_COUNT
} klass_operator_type_t;
/*
 * Write the #documentation.
 */
typedef value_t(*builtin_klass_operator_t)(object_t *object, value_t arg);

/*
 * Write the #documentation.
 */
struct builtin_class_data
{
   builtin_klass_operator_t operators[KLASS_OPERATOR_COUNT];
   builtin_klass_operator_t operator_add;
   builtin_klass_operator_t operator_sub;
   builtin_klass_operator_t operator_mul;
   builtin_klass_operator_t operator_div;
   builtin_klass_operator_t operator_mod;
};

/*
 * Write the #documentation.
 */
union class_data
{
   struct builtin_class_data builtin;
};

/*
 * Write the #documentation.
 */
typedef struct klass
{
   object_t object;
   klass_type_t type;
   union class_data data;
} klass_t;

/*
 * Write the #documentation.
 */
typedef struct
{
   object_t    object;
   uint        size;
   char        chars[];
} string_t;

#define BITS_NAN_MASK      ((uint64_t)(0x7ff4000000000000))
#define BITS_TAG_MASK      ((uint64_t)(0xffff000000000000))
#define BITS_TAG_PAYLOAD   ~BITS_TAG_MASK
#define BITS_TAG_BOOLEAN   ((uint64_t)(0x7ffc000000000000))
#define BITS_TAG_OBJECT    ((uint64_t)(0xfffc000000000000))

/*
 * Write the #documentation.
 */
inline value_t value_nil(void)
{
   return (value_t) { .bits = BITS_NAN_MASK };
}

/*
 * Write the #documentation.
 */
inline value_t value_boolean(bool boolean)
{
   return (value_t) { .bits = BITS_TAG_BOOLEAN | boolean };
}

/*
 * Write the #documentation.
 */
inline value_t value_number(kj_number_t number)
{
   return (value_t) { .number = number };
}

/*
 * Write the #documentation.
 */
inline value_t value_object(void const *ptr)
{
   assert(((uint64_t)ptr & BITS_TAG_MASK) == 0 && "Pointer with some bit higher than 48th set.");
   return (value_t) { .bits = (BITS_TAG_OBJECT | (uint64_t)ptr) };
}

/*
 * Write the #documentation.
 */
inline bool value_is_nil(value_t value)
{
   return value.bits == BITS_NAN_MASK;
}

/*
 * Write the #documentation.
 */
inline bool value_is_boolean(value_t value)
{
   return (value.bits & BITS_TAG_MASK) == BITS_TAG_BOOLEAN;
}

/*
 * Write the #documentation.
 */
inline bool value_is_number(value_t value)
{
   return (value.bits & BITS_NAN_MASK) != BITS_NAN_MASK;
}

/*
 * Write the #documentation.
 */
inline  bool value_is_object(value_t value)
{
   return (value.bits & BITS_TAG_MASK) == BITS_TAG_OBJECT;
}

/*
 * Write the #documentation.
 */
inline  bool value_get_boolean(value_t value)
{
   assert(value_is_boolean(value));
   return (value.bits & (uint64_t)1);
}

/*
 * Write the #documentation.
 */
inline bool value_to_boolean(value_t value)
{
   if (value_is_boolean(value)) return value_get_boolean(value);
   else if (value_is_number(value)) return value.number != 0;
   else if (value_is_nil(value)) return false;
   return true;
}

/*
 * Write the #documentation.
 */
inline object_t* value_get_object(value_t value)
{
   assert(value_is_object(value));
   return (void*)(intptr_t)(value.bits & BITS_TAG_PAYLOAD);
}

/*
 * Write the #documentation.
 */
kj_intern value_t value_new_string(allocator_t *alloc, klass_t *string_class, uint length);

/*
 * Write the #documentation.
 */
kj_intern value_t value_new_stringf(allocator_t *alloc, klass_t *string_class, const char *format, ...);

/*
 * Write the #documentation.
 */
kj_intern value_t value_new_stringfv(allocator_t *alloc, klass_t *string_class, const char *format, va_list args);

/*
 * Write the #documentation.
 */
kj_intern void value_destroy(value_t *value, allocator_t *alloc);

/*
 * Write the #documentation.
 */
kj_intern void value_set(value_t * dest, allocator_t * allocator, value_t const *src);

/*
 * Write the #documentation.
 */
inline void value_move(value_t * dest, allocator_t * allocator, value_t src)
{
   value_destroy(dest, allocator);
   *dest = src;
}

/*
 * Write the #documentation.
 */
inline void value_set_nil(value_t * dest, allocator_t * allocator)
{
   value_destroy(dest, allocator);
   *dest = value_nil();
}

/*
 * Write the #documentation.
 */
inline void value_set_boolean(value_t * dest, allocator_t * allocator, bool value)
{
   value_destroy(dest, allocator);
   *dest = value_boolean(value);
}

/*
 * Write the #documentation.
 */
inline void value_set_number(value_t * dest, allocator_t * allocator, kj_number_t value)
{
   value_destroy(dest, allocator);
   dest->number = value;
}

/*
 * Write the #documentation.
 */
kj_intern const char * value_type_str(value_t value);

/*
 * Write the #documentation.
 */
kj_intern void string_class_init(klass_t* c);