/*
 * koji - opcodes and bytecode manipulation
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kvalue.h"
#include "kstring.h"

kintern const char*
value_type_str(union value v)
{
	if (value_isnil(v)) return "nil";
	if (value_isbool(v)) return "bool";
	if (value_isnum(v)) return "number";
	return "object";
}

kintern void
value_const_destroy(union value c, struct koji_allocator *alloc)
{
	if (value_isobj(c)) {
      /* the only allowed objects as constants are strings */
      struct string *s = value_getobjv(c);
      struct class *cls_str = s->object.class;
      string_free(s, alloc);
      assert(cls_str->object.refs > 1);
      --cls_str->object.refs;
	}
}
