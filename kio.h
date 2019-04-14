/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "kplatform.h"

/*
 *
 */
struct membuf {
	char *curr;
	char *end;
};

/*
 * Opens a file input stream at specified [filename] and returns if file open
 * was successful.
 */
kintern bool
koji_source_open_file(const char *filename, struct koji_source *);

/*
 * Closes a previously opened file input stream.
 */
kintern void
koji_source_file_close(struct koji_source *);

/*
 * Opens a string input stream.
 */
kintern void
koji_source_open_string(const char *name, const char **string,
                        struct koji_source *);

/*
 * Opens a memory input stream with specified [name] and memory buffer [mb].
 */
kintern void
koji_source_open_membuf(const char *name, struct membuf *mb,
                        struct koji_source *);
