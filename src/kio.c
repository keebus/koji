/*
 * koji scripting language
 *
 * Copyright (C) 2017 Canio Massimo Tristano
 *
 * This source file src part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#include "kio.h"

#pragma warning(push, 0)
#include <stdio.h>
#pragma warning(pop)

static int
source_file_read(FILE *file)
{
   return fgetc(file);
}

static int
source_string_read(const char **str)
{
   if (*str == 0)
      return KOJI_EOF;
   return *(*str)++;
}

static int
source_mem_read(struct source_membuf *mb)
{
   if (mb->curr >= mb->end)
      return KOJI_EOF;
   return *mb->curr++;
}

kintern kbool
source_file_open(struct koji_source *src, const char *filename)
{
   FILE *file = fopen(filename, "r");
   if (!file)
      return kfalse;
   src->name = filename;
   src->fn = (koji_source_read_t)source_file_read;
   src->user = file;
   return ktrue;
}

kintern void
source_file_close(struct koji_source *src)
{
   fclose((FILE *)src->user);
}

kintern void
source_string_open(struct koji_source *src, const char *name,
   const char **string)
{
   src->name = name;
   src->fn = (koji_source_read_t)source_string_read;
   src->user = (void*)string;
}

kintern void
source_mem_open(struct koji_source *src, const char *name,
   struct source_membuf *mb)
{
   src->name = name;
   src->fn = (koji_source_read_t)source_mem_read;
   src->user = mb;
}
