/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
#pragma once

#include "kj_support.h"
#include <assert.h>

typedef enum {
   VALUE_NIL,
   VALUE_BOOL,
   VALUE_NUMBER,
   VALUE_OBJECT,
} value_type_t;

typedef union {
   double   number;
   uint64_t bits;
} value_t;

#define BITS_NAN_MASK      ((uint64_t)(0x7ff4000000000000))
#define BITS_TAG_MASK      ((uint64_t)(0xffff000000000000))
#define BITS_TAG_PAYLOAD   ~BITS_TAG_MASK
#define BITS_TAG_OBJECT    ((uint64_t)(0xfffc000000000000))

static inline value_t value_nil(void) {
   return (value_t) { .bits = BITS_NAN_MASK };
}

static inline value_t value_number(koji_number_t number) {
   return (value_t) { .number = number };
}

static inline value_t value_object(void const *ptr) {
   assert(((uint64_t)ptr & 0xffff000000000000) == 0 && "Pointer with some bit higher than 48th set.");
   return (value_t) { .bits = (BITS_TAG_OBJECT | (uint64_t)ptr) };
}

static inline bool value_is_nil(value_t value) {
   return value.bits == BITS_NAN_MASK;
}

static inline bool value_is_number(value_t value) {
      return (value.bits & BITS_NAN_MASK) != BITS_NAN_MASK;
}

static inline bool value_is_object(value_t value)
{
   return (value.bits & BITS_TAG_MASK) == BITS_TAG_OBJECT;
}

static void* value_get_object(value_t value)
{
   assert(value_is_object(value));
   return (void*)(intptr_t)(value.bits & BITS_TAG_PAYLOAD);
}
