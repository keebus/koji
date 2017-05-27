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
value_type_str(union value val)
{
	if (value_isnil(val)) return "nil";
	if (value_isbool(val)) return "bool";
	if (value_isnum(val)) return "number";
	return "object";
}
