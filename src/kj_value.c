/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
#include "kj_value.h"
#include "kj_support.h"

kj_intern value_t value_new_string(allocator_t *alloc, uint length, class_t *string_class)
{
   string_t *string = kj_malloc(sizeof(string_t) + length + 1, kj_alignof(string_t), alloc);
   string->object.references = 1;
   string->object.class = string_class;
   string->size = length;
   return value_object(string);
}
