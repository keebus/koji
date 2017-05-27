/*
 * koji scripting language
 * 
 * Copyright (C) 2017 Canio Massimo Tristano
 * 
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kplatform.h"

struct source_membuf {
   char *curr;
   char *end;
};

/*
 * Opens a file input stream at specified [filename] and returns if file open
 * was successful.
 */
kintern kbool
source_file_open(struct koji_source *src, const char *filename);

/*
 * Closes a previously opened file input stream.
 */
kintern void
source_file_close(struct koji_source *src);

/*
 * Opens a string input stream.
 */
kintern void
source_string_open(struct koji_source *src, const char *name,
   const char **string);

/*
 * Opens a memory input stream with specified [name] and memory buffer [mb].
 */
kintern void
source_mem_open(struct koji_source *src, const char *name,
   struct source_membuf *mb);
