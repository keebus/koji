/*
 * koji - opcodes and bytecode manipulation
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kstring.h"
#include "kvm.h"

#include <string.h>
#include <stdio.h>

kintern struct string *
string_new(struct class *cls_string, struct koji_allocator *alloc, int32_t len)
{
	struct string *str = alloc->alloc(sizeof(struct string) + len, alloc->user);
	++cls_string->object.refs;
	str->object.refs = 1;
	str->object.class = cls_string;
	str->len = len;
	return str;
}

kintern void
string_free(struct string *str, struct koji_allocator *alloc)
{
   assert(str->object.class->object.refs > 1);
   alloc->free(str, sizeof(*str) + str->len, alloc->user);
}

kintern union value
value_new_string(struct class *cls_string, struct koji_allocator *alloc,
   int32_t len)
{
	struct string *str = string_new(cls_string, alloc, len);
	if (!str) return value_nil();
	return value_obj(str);
}

kintern union value
value_new_stringfv(struct class *cls_string, struct koji_allocator *alloc,
   const char *format, va_list args)
{
	int32_t len = vsnprintf(NULL, 0, format, args);
	struct string *str = string_new(cls_string, alloc, len);
	if (!str) value_nil();
	vsnprintf(&str->chars, len + 1, format, args);
	return value_obj(str);
}

kintern union value
value_new_stringf(struct class *cls_string, struct koji_allocator *alloc,
   const char* format, ...)
{
	va_list args;
   union value value;
	va_start(args, format);
	value = value_new_stringfv(cls_string, alloc, format, args);
	va_end(args);
	return value;
}

/* string class related */

static void
string_dtor(struct vm* vm, struct object *obj)
{
   string_free((struct string *)obj, &vm->alloc);
}

static union class_op_result
string_op_add(struct vm* vm, struct object *obj, enum class_op_kind op,
   union value arg1, union value arg2)
{
   struct class *cls = obj->class;
	struct string *lstr = (struct string *)obj;  /* lhs str */
	struct string *rstr = value_getobjv(arg1); /* rhs str */
   union class_op_result res;

	if (value_isobj(arg1) && rstr->object.class == cls) {
      struct string *res_str;
		res.value = value_new_string(cls, &vm->alloc, lstr->len + rstr->len);
      res_str = value_getobjv(res.value);
		memcpy(&res_str->chars, &lstr->chars, lstr->len);
		memcpy(&res_str->chars + lstr->len, &rstr->chars, rstr->len + 1);
		return res;
	}

	return class_op_invalid(vm, obj, op, arg1, arg2);
}

static union class_op_result
string_op_mul(struct vm* vm, struct object *obj, enum class_op_kind op,
   union value arg1, union value arg2)
{
   struct class *cls = obj->class;
	struct string *lstr = (struct string *)obj;
   union class_op_result res;

	if (!value_isnum(arg1)) {
		class_op_invalid(vm, obj, op, arg1, arg2);
      res.value = value_nil();
		return res;
	}

	int32_t mult = (int32_t)arg1.num;
	int32_t str_len = lstr->len;
	res.value = value_new_string(cls, &vm->alloc, str_len * mult);
	struct string *res_str = value_getobjv(res.value);

	int32_t offset = 0;
	for (int32_t i = 0; i < mult; ++i) {
		memcpy(&res_str->chars + offset, &lstr->chars, str_len);
		offset += str_len;
	}

	(&res_str->chars)[res_str->len] = 0;
	return res;
}

static union class_op_result
string_op_compare(struct vm* vm, struct object *obj, enum class_op_kind op,
   union value arg1, union value arg2)
{
   struct class *cls = obj->class;
	struct string *lstr = (struct string *)obj;
	struct string *rstr = value_getobjv(arg1);

   if (value_isobj(arg1) && rstr->object.class == cls) {
	   union class_op_result res;
      res.compare =  lstr->len < rstr->len ? -1 :
						   lstr->len > rstr->len ? 1 :
						   memcmp(&lstr->chars, &rstr->chars, lstr->len);
      return res;
   }

	return class_op_compare_default(vm, obj, op, arg1, arg2);
}

static union class_op_result
string_op_hash(struct vm* vm, struct object *obj, enum class_op_kind op,
   union value arg1, union value arg2)
{
	struct string *lstr = (struct string *)obj;
   union class_op_result res;
   res.hash = murmur2(&lstr->chars, lstr->len, 0);
   return res;
}

static union class_op_result
string_op_get(struct vm* vm, struct object *obj, enum class_op_kind op,
   union value arg1, union value arg2)
{
	struct string *lstr = (struct string *)obj;

   union class_op_result res;
   res.value = value_num((&lstr->chars)[(uint32_t)arg1.num]);
	return res;
}

static union class_op_result
string_op_set(struct vm* vm, struct object *object, enum class_op_kind op,
   union value arg1, union value arg2)
{
	struct string *lstr = (struct string *)object;
	char ch = (&lstr->chars)[(uint32_t)arg1.num] = (char)arg2.num;

   union class_op_result res;
   res.value = value_num(ch);
	return res;
}

kintern void
class_string_init(struct class *cls_string, struct class *cls_builtin)
{
   class_init_default(cls_string, cls_builtin, "string");
	cls_string->dtor = string_dtor;
	cls_string->operator[CLASS_OP_ADD] = string_op_add;
	cls_string->operator[CLASS_OP_MUL] = string_op_mul;
	cls_string->operator[CLASS_OP_COMPARE] = string_op_compare;
	cls_string->operator[CLASS_OP_HASH] = string_op_hash;
	cls_string->operator[CLASS_OP_GET] = class_op_invalid; /* todo */
	cls_string->operator[CLASS_OP_SET] = class_op_invalid; /* todo */
}
