/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

#pragma once

#include "kj_support.h"

/*
 * Implements the stream reading function from a memory buffer containing the script source code.
 * [data] must be the [const char*] to the string.
 */
kj_intern int stream_read_string(void *data);

/*
 * Implements the stream reading function from a file. [data] must be the FILE*.
 */
kj_intern int stream_read_file(void *data);
