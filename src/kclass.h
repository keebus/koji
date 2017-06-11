/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kvalue.h"

struct class_member {
   const char *name;
   koji_function_t func;
};

typedef uint64_t (* class_op_hash_t) (struct object *);

struct class {
   struct object object;
   const char *name;
   int32_t size;
   int32_t memberslen;
   uint64_t seed;
   class_op_hash_t hash;
   struct class_member members[];
};

kintern struct class *
class_new(struct class *class_class, const char *name, int32_t namelen,
   const char **members, int32_t nmembers, struct koji_allocator *alloc);

kintern void
class_free(struct class *class, struct koji_allocator *alloc);

kintern struct class_member *
class_getmember(struct class *class, const char *member, int memberlen);
