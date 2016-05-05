/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
#include "kj_api.h"

#ifndef NULL
#define NULL (void*)(0)
#endif

int main(int argc, char **argv)
{
   (void)argc;
   (void)argv;
   
   koji_state *koji = koji_open(NULL, NULL, NULL, NULL);

   koji_close(koji);

   return 0;
}
