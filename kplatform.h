/*
 * koji scripting language
 *
 * Copyright (C) 2019 Canio Massimo Tristano
 *
 * This source file is part of the koji scripting language, distributed under
 * the MIT license. See koji.h for further licensing information.
 */

#pragma once

#include "koji.h"

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN64
#define KOJI_64
#endif

/*
 * Platform independent language extensions
 */

#ifdef KOJI_AMALGAMANTE
#define kintern static
#else
#define kintern
#endif

#define kalignof(T) __alignof(T)
#define kalloca _alloca

/*
 * Mathematical functions.
 */

static inline int32_t
min_i32(int32_t a, int32_t b)
{
	return a < b ? a : b;
}

static inline int32_t
max_i32(int32_t a, int32_t b)
{
	return a > b ? a : b;
}

static inline int32_t
min_u32(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

static inline uint32_t
max_u32(uint32_t a, uint32_t b)
{
	return a > b ? a : b;
}

#ifdef KOJI_64

typedef uint64_t hash_t;

/*
 * Computes and returns the 64-bit hash of uint64 value `x`
 */
static inline hash_t
hash(hash_t x)
{
	x ^= x >> 32;
	x *= UINT64_C(0xd6e8feb86659fd93);
	x ^= x >> 32;
	x *= UINT64_C(0xd6e8feb86659fd93);
	x ^= x >> 32;
	return x;
}

#else

typedef uint32_t hash_t;

static inline hash_t
hash(hash_t x)
{
	x ^= x >> 16;
	x *= UINT32_C(0x7feb352d);
	x ^= x >> 15;
	x *= UINT32_C(0x846ca68b);
	x ^= x >> 16;
	return x;
}

#endif
