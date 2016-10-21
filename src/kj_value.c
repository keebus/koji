/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
#include "kj_value.h"
#include "kj_support.h"
#include <stdio.h>

static bool object_deref(object_t *object)
{
   return --object->references == 0;
}

kj_intern string_t * string_new(allocator_t *alloc, class_t *class_string, uint length)
{
   string_t *string = kj_malloc(sizeof(string_t) + length + 1, kj_alignof(string_t), alloc);
   if (!string) return NULL;
   ++class_string->object.references;
   string->object.references = 1;
   string->object.class = class_string;
   string->size = length;
   return string;
}

kj_intern value_t value_new_stringf(allocator_t *alloc, class_t *string_class, const char *format,
                                    ...)
{
   va_list args;
   va_start(args, format);
   value_t value = value_new_stringfv(alloc, string_class, format, args);
   va_end(args);
   return value;
}

kj_intern value_t value_new_stringfv(allocator_t *alloc, class_t *string_class, const char *format,
                                     va_list args)
{
   uint length = vsnprintf(NULL, 0, format, args);
   string_t *string = string_new(alloc, string_class, length);
   vsnprintf(string->chars, length + 1, format, args);
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

kj_intern void value_set(value_t * dest, allocator_t * allocator, value_t * src)
{
   if (dest == src) return;
	
   value_destroy(dest, allocator);
	*dest = *src;

   /* if value is an object, bump up its reference count */
   if (value_is_object(*dest))
   {
      ++value_get_object(*dest)->references;
   }
}

kj_intern const char * value_type_str(value_t value)
{
   if (value_is_nil(value)) return "nil";
   if (value_is_boolean(value)) return "bool";
   if (value_is_number(value)) return "number";
   return "object";
}
