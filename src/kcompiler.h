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
   uint8_t *scratch_beg; /* beginning of the scratch memory buffer */
   uint8_t *scratch_end; /* end of the scratch memory buffer */
	struct class *cls_string; /* pointer to the str class */
};

/*
 * Compiles a stream containing a source str to a koji module and returns
 * the prototype generated that represents the compiled module.
 */
kintern koji_result_t
compile(struct compile_info *, struct prototype **proto);
