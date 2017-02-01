/*
 * koji scripting language
 * 2016 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#include "kj_io.h"
#include "koji.h"
#include <stdio.h>

kj_intern int stream_read_string(void *data)
{
   const char **stream = data;
   if (**stream) return *((*stream)++);
   return KOJI_EOF;
}

kj_intern int stream_read_file(void *data)
{
   return fgetc(data);
}
