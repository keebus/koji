/*
 * koji - opcodes and bytecode manipulation
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kstring.h"

#include <stdio.h>
#include <string.h>

kintern struct string *
string_new(struct class *class_string, struct koji_allocator *alloc,
           int32_t len)
{
	struct string *s =
	    alloc->allocate(sizeof(struct string) + len + 1, alloc->user);
	s->object.refs = 1;
	s->object.class = class_string;
	s->len = len;
	return s;
}

kintern void
string_free(struct string *s, struct koji_allocator *alloc)
{
	alloc->deallocate(s, sizeof(*s) + s->len + 1, alloc->user);
}

kintern union value
value_new_string(struct class *class_string, struct koji_allocator *alloc,
                 int32_t len)
{
	struct string *s = string_new(class_string, alloc, len);
	++class_string->object.refs;
	return value_obj(s);
}

kintern union value
value_new_stringfv(struct class *class_string, struct koji_allocator *alloc,
                   const char *format, va_list args)
{
	int32_t len = vsnprintf(NULL, 0, format, args);
	union value s = value_new_string(class_string, alloc, len);
	vsnprintf(((struct string *)value_getobj(s))->chars, len + 1, format, args);
	return s;
}

kintern union value
value_new_stringf(struct class *class_string, struct koji_allocator *alloc,
                  const char *format, ...)
{
	va_list args;
	union value value;
	va_start(args, format);
	value = value_new_stringfv(class_string, alloc, format, args);
	va_end(args);
	return value;
}

/* string class related */
#if 0
static void
string_dtor(struct vm *vm, struct object *obj)
{
	string_free((struct string *)obj, &vm->allocator);
}

static union class_op_result
string_op_add(struct vm *vm, struct object *obj, enum class_op_id op,
              union value arg1, union value arg2)
{
	assert(!value_isnil(arg1) && value_isnil(arg2));
	struct class *cls = obj->class;
	struct string *lstr = (struct string *)obj; /* lhs str */
	struct string *rstr = value_getobjv(arg1);  /* rhs str */
	union class_op_result res;

	if (value_isobj(arg1) && rstr->object.class == cls) {
		struct string *res_str;
		res.value = value_new_string(cls, &vm->allocator, lstr->len + rstr->len);
		res_str = value_getobjv(res.value);
		memcpy(res_str->chars, &lstr->chars, lstr->len);
		memcpy(res_str->chars + lstr->len, &rstr->chars, rstr->len + 1);
		return res;
	}

	return class_op_invalid(vm, obj, op, arg1, arg2);
}

static union class_op_result
string_op_mul(struct vm *vm, struct object *obj, enum class_op_id op,
              union value arg1, union value arg2)
{
	assert(!value_isnil(arg1) && value_isnil(arg2));
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
	res.value = value_new_string(cls, &vm->allocator, str_len * mult);
	struct string *res_str = value_getobjv(res.value);

	int32_t offset = 0;
	for (int32_t i = 0; i < mult; ++i) {
		memcpy(res_str->chars + offset, &lstr->chars, str_len);
		offset += str_len;
	}

	res_str->chars[res_str->len] = 0;
	return res;
}

static union class_op_result
string_op_compare(struct vm *vm, struct object *obj, enum class_op_id op,
                  union value arg1, union value arg2)
{
	assert(!value_isnil(arg1) && value_isnil(arg2));
	struct class *cls = obj->class;
	struct string *lstr = (struct string *)obj;
	struct string *rstr = value_getobjv(arg1);

	if (value_isobj(arg1) && rstr->object.class == cls) {
		union class_op_result res;
		res.compare = lstr->len < rstr->len
		                  ? -1
		                  : lstr->len > rstr->len
		                        ? 1
		                        : memcmp(&lstr->chars, &rstr->chars, lstr->len);
		return res;
	}

	return class_op_default_compare(vm, obj, op, arg1, arg2);
}

static union class_op_result
string_op_hash(struct vm *vm, struct object *obj, enum class_op_id op,
               union value arg1, union value arg2)
{
	assert(value_isnil(arg1) && value_isnil(arg2));
	struct string *lstr = (struct string *)obj;
	union class_op_result res;
	res.hash = murmur2(&lstr->chars, lstr->len, 0);
	return res;
}

static union class_op_result
string_op_get(struct vm *vm, struct object *obj, enum class_op_id op,
              union value arg1, union value arg2)
{
	assert(!value_isnil(arg1) && value_isnil(arg2));
	struct string *lstr = (struct string *)obj;
	union class_op_result res;
	res.value = value_num(lstr->chars[(uint32_t)arg1.num]);
	return res;
}

static union class_op_result
string_op_set(struct vm *vm, struct object *object, enum class_op_id op,
              union value arg1, union value arg2)
{
	assert(!value_isnil(arg1) && !value_isnil(arg2));
	struct string *lstr = (struct string *)object;
	char ch = lstr->chars[(uint32_t)arg1.num] = (char)arg2.num;
	union class_op_result res;
	res.value = value_num(ch);
	return res;
}

kintern void
class_string_init(struct class *class_string, struct class *cls_builtin)
{
	class_init_default(class_string, cls_builtin, "string");
	class_string->dtor = string_dtor;
	class_string->operator[CLASS_OP_ADD] = string_op_add;
	class_string->operator[CLASS_OP_MUL] = string_op_mul;
	class_string->operator[CLASS_OP_COMPARE] = string_op_compare;
	class_string->operator[CLASS_OP_HASH] = string_op_hash;
	class_string->operator[CLASS_OP_GET] = class_op_invalid; /* todo */
	class_string->operator[CLASS_OP_SET] = class_op_invalid; /* todo */
}

#endif