/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kclass.h"
#include "kvm.h"
#include <stdio.h>
#include <string.h>

static int
class_dtor_default(koji_t vm, void *user, enum koji_op op, int nargs)
{
	return 0; /* nothing to do */
}

static uint64_t
class_op_hash_default(struct object *obj)
{
	return (uint64_t)obj;
}

kintern struct class*
class_new(struct class *class_class, const char *name, int32_t namelen,
   const char **members, int32_t nmembers,
   struct koji_allocator *alloc)
{
   uint64_t seed = 0;
   int32_t memberslen = nmembers;
   bool *memused = kalloca(nmembers);
   int32_t *memsizes = kalloca(nmembers * sizeof(int32_t));
   int32_t memnamelentot = 0;

   for (int32_t i = 0; i < nmembers; ++i) {
      int32_t len = strlen(members[i]);
      memsizes[i] = len;
      memnamelentot += len + 1;
   }

retry:
   memberslen += memberslen / 3;
   memset(memused, 0, nmembers);
   seed = mix64(seed + 1);
   for (int32_t i = 0; i < nmembers; ++i) {
      uint64_t hash = murmur2(members[i], memsizes[i], seed);
      int32_t index = hash % memberslen;
      if (memused[index])
         goto retry;
      memused[index] = true;
   }

   int32_t allmemberslen = memberslen + KOJI_OP_USER0;

   int32_t size = sizeof(struct class)
      + namelen + 1
      + allmemberslen * sizeof(struct class_member)
      + memnamelentot;

   struct class *cls = alloc->alloc(size, alloc->user);
   cls->object.refs = 1;
   cls->object.class = class_class;
   cls->size = size;
   cls->memberslen = memberslen;
   cls->seed = seed;
   cls->hash = class_op_hash_default;

   memset(cls->members, 0, allmemberslen * sizeof(struct class_member));

   char *names = (char*)(cls->members + allmemberslen);
   cls->name = names;
   memcpy(cls->name, name, namelen + 1);
   names += namelen + 1;

   for (int32_t i = 0; i < memberslen; ++i) {
      uint32_t idx = KOJI_OP_USER0 + i;
      cls->members[idx].name = names;
      memcpy(names, members[i], memsizes[i] + 1);
      names += memsizes[i] + 1;
   }

   return cls;
}

kintern void
class_free(struct class *class, struct koji_allocator *alloc)
{
   alloc->free(class, class->size, alloc->user);
}

kintern struct class_member *
class_getmember(struct class *class, const char *member, int memberlen)
{
   uint32_t index = (uint32_t)(murmur2(member, memberlen, class->seed)
      % class->memberslen);

   struct class_member *m = class->members + KOJI_OP_USER0 + index;
   if (memcmp(member, m->name, memberlen + 1) != 0)
      return NULL;

   return m;
}
