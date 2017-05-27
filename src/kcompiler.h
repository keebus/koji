/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kerror.h"

/*
 * This structure wraps all the information to compile one source stream. It is
 * populated by the client and consumed by the compile() function. All fields
 * are mandatory.
 */
struct compile_info {
	struct koji_allocator alloc; /* the memory allocator to use */
	struct koji_source *source; /* the source stream */
	struct issue_handler issue_handler; /* used to report compilation issues */
	struct class *cls_string; /* pointer to the str class */
};

/*
 * Compiles a stream containing a source str to a koji module and returns
 * the prototype generated that represents the compiled module.
 */
kintern struct prototype *
compile(struct compile_info *);
