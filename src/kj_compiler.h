/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "kj_support.h"
#include "kj_error.h"
#include "kj_value.h"

/*
 * This structure wraps all the information to compile one source stream. It is populated by the
 * client and consumed by the compile() function. All fields are mandatory.
 */
typedef struct {
   /* the allocator to use for all allocation operations */
   allocator_t *allocator;
   /* the source stream "name", e.g. for source files it might be the file path */
   const char *source_name;
   /* the stream function used to read the source stream */
   koji_stream_read_t stream_fn;
   /* the stream context data passed to the stream function on character read */
   void *stream_data;
   /* the issue reporter that will be used to report compilation issues */
   issue_reporter_t issue_reporter_fn;
   /* issue reporter user data passed to the issue reporter function */
   void *issue_reporter_data;
   /* the string class */
   class_t * class_string;
} compile_info_t;

/*
 * Compiles a stream containing a source string to a koji module, reporting any error to specified
 * @error_handler (todo). This function is called by the koji_state in its load functions.
 */
kj_intern struct prototype * compile(compile_info_t const *info);
