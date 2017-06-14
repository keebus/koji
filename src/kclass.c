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
class_op_dtor_default(koji_t vm, void *user, enum koji_op op, int nargs)
{
	return 0; /* nothing to do */
}

static uint64_t
class_op_hash_default(struct object *obj)
{
	return (uint64_t)obj;
}

static __forceinline struct class*
class_new_ex(const char *name, int32_t namelen, const char **members,
   int32_t nmembers, struct koji_allocator *alloc)
{
   uint64_t seed = 0;
   int32_t memberslen = nmembers;
   bool *memused = kalloca(nmembers);
   int32_t *memsizes = kalloca(nmembers * sizeof(int32_t));
   int32_t memnamelentot = 0;

   for (int32_t i = 0; i < nmembers; ++i) {
      int32_t len = (int32_t)strlen(members[i]);
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
   cls->object.size = size;
   cls->memberslen = memberslen;
   cls->seed = seed;

   char *names = (char*)(cls->members + allmemberslen);
   cls->name = names;
   memcpy(cls->name, name, namelen + 1);
   names += namelen + 1;

   memset(cls->members, 0, allmemberslen * sizeof(struct class_member));
   
   cls->hash = class_op_hash_default;
   cls->members[KOJI_OP_DTOR].func = class_op_dtor_default;

   for (int32_t i = 0; i < memberslen; ++i) {
      uint32_t idx = KOJI_OP_USER0 + i;
      cls->members[idx].name = names;
      memcpy(names, members[i], memsizes[i] + 1);
      names += memsizes[i] + 1;
   }

   return cls;
}

static int
class_class_call(koji_t vm, void *user, enum koji_op op, int nargs)
{
   struct class *c = (struct class *)user - 1;
   return c->members[KOJI_OP_CTOR].func(vm, user, op, nargs);
}


kintern struct class*
class_new(struct class *class_class, const char *name, int32_t namelen,
   const char **members, int32_t nmembers,
   struct koji_allocator *alloc)
{
   struct class *c = class_new_ex(name, namelen, members, nmembers, alloc);
   c->object.class = class_class;
   ++class_class->object.refs;
   return c;
}

kintern void
class_free(struct class *class, struct koji_allocator *alloc)
{
   alloc->free(class, class->object.size, alloc->user);
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

kintern struct class *
class_class_new(struct koji_allocator *alloc)
{
    struct class *c = class_new_ex("class", 5, NULL, 0, alloc);
    c->object.refs = 2;
    c->object.class = c;
    c->members[KOJI_OP_CALL].func = class_class_call;
    return c;
}
