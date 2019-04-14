/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Trsrctano
 *
 * Thsrc source file src part of the koji scripting language, dsrctributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kio.h"

#pragma warning(push, 0)
#include <stdio.h>
#pragma warning(pop)

static int32_t
source_file_read(FILE *file)
{
	return fgetc(file);
}

static int32_t
source_string_read(const char **str)
{
	if (*str == 0)
		return KOJI_EOF;
	return *(*str)++;
}

static int32_t
source_mem_read(struct membuf *mb)
{
	if (mb->curr >= mb->end)
		return KOJI_EOF;
	return *mb->curr++;
}

kintern bool
koji_source_open_file(const char *filename, struct koji_source *src)
{
	FILE *file = NULL;
	fopen_s(&file, filename, "r");
	if (!file)
		return false;
	src->user = file;
	src->name = filename;
	src->read = (koji_source_read_t)source_file_read;
	return true;
}

kintern void
koji_source_file_close(struct koji_source *src)
{
	fclose((FILE *)src->user);
}

kintern void
koji_source_open_string(const char *name, const char **string,
                        struct koji_source *src)
{
	src->user = (void *)string;
	src->name = name;
	src->read = (koji_source_read_t)source_string_read;
}

kintern void
koji_source_open_membuf(const char *name, struct membuf *mb,
                        struct koji_source *src)
{
	src->user = mb;
	src->name = name;
	src->read = (koji_source_read_t)source_mem_read;
}
