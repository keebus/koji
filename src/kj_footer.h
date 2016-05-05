/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */

/*
 * This file is automatically pasted into 'koji.c' by the amalgamate script at the end of the source
 * file. This file cleanly pops the compiler diagnostic state used to suppress erraneous warnings
 * for various compilers.
 */
 
 /* these states are pushed in kj_support.h */
 #if defined(__GNUC_)
   #pragma GCC diagnostic pop 
 #elif defined(__clang__)
   #pragma clang diagnostic pop
 #elif defined(_MSC_VER)
   #pragma warning(pop)
 #endif
 