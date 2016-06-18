/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
#include "kj_value.h"
#include "kj_support.h"

static bool object_deref(object_t *object)
{
   return --object->references == 0;
}

kj_intern value_t value_new_string(allocator_t *alloc, uint length, class_t *string_class)
{
   string_t *string = kj_malloc(sizeof(string_t) + length + 1, kj_alignof(string_t), alloc);
   string->object.references = 1;
   string->object.class = string_class;
   string->size = length;
   return value_object(string);
}

kj_intern void value_destroy(value_t *value, allocator_t *alloc)
{
   if (value_is_object(*value))
   {
      object_t *object = value_get_object(*value);
      if (object_deref(object)) {
         if (object_deref((object_t*)object->class))
            kj_free(object->class, alloc);
         kj_free(object, alloc);
      }
   }
}
