/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
#include "kj_api.h"
#include "kj_compiler.h"
#include "kj_bytecode.h"
#include "kj_io.h"
#include "kj_support.h"
#include "kj_value.h"
#include "kj_vm.h"

#include <stdio.h>
#include <string.h>

struct koji_state {
   vm_t vm;
};

/*
 * This issue handler wraps the state so that all issues that need reporting are pushed as strings
 * onto the stack. It also tracks the number of issues that it reports.
 */
typedef struct {
   koji_state_t * state;
   uint           issue_count;
} load_issue_handler_data_t;


static void load_issue_reporter_fn(void *data, source_location_t sloc, const char *message)
{
   (void)sloc;
   load_issue_handler_data_t *issue_handler_data = data;

   ++issue_handler_data->issue_count;

   (void)message;
   printf("(TEMP) error: %s\n", message);
   //koji_push_string(issue_handler_data->state, message, (uint)strlen(message));
}

/*
#define state_throw_error(s, format, ...) \
  kj_push_stringf(s, format, __VA_ARGS__), longjmp(s->error_jump_buf, 1)
*/

KOJI_API koji_state_t * koji_open(koji_malloc_fn_t malloc_fn, koji_realloc_fn_t realloc_fn,
                                  koji_free_fn_t free_fn, void *alloc_userdata)
{
   /* setup the allocator object */
   allocator_t allocator = {
      .user_data = alloc_userdata,
      .malloc = malloc_fn ? malloc_fn : default_malloc,
      .realloc = realloc_fn ? realloc_fn : default_realloc,
      .free = free_fn ? free_fn : default_free,
   };

   koji_state_t *state = kj_alloc(koji_state_t, 1, &allocator);
   
   /* initialize the Virtual Machine within the state */
   vm_init(&state->vm, allocator);

   return state;
}

KOJI_API koji_result_t koji_load(koji_state_t *state, const char *source_name,
                                 koji_stream_read_t stream_read_fn, void *stream_read_data)
{
  prototype_t *proto = compile(&(compile_info_t) {
      .allocator = &state->vm.allocator,
      .source_name = source_name,
      .stream_fn = stream_read_fn,
      .stream_data = stream_read_data,
      .issue_reporter_fn = load_issue_reporter_fn,
      .issue_reporter_data = &(load_issue_handler_data_t) { .state = state },
      .class_string = &state->vm.class_string,
  });

  /* some error occurred and the prototype could not be compiled, report the error. */
  if (!proto) return KOJI_ERROR;

  /* temporary */
  prototype_dump(proto, 0, &state->vm.class_string);

  /* reset the number of references as the ref count will be increased when the prototype is
   * referenced by a the new frame */
  proto->references = 0;

  /* create a closure to main prototype and push it to the stack */
  vm_push_frame(&state->vm, proto, 0);

  return KOJI_SUCCESS;
}

KOJI_API koji_result_t koji_load_string(koji_state_t *state, const char *source)
{
   return koji_load(state, "<string>", stream_read_string, (void*)&source);
}

KOJI_API koji_result_t koji_load_file(koji_state_t *state, const char *filename)
{
   /* try opening the file and report an error if file could not be open */
   FILE *file = NULL;
   fopen_s(&file, filename, "r");

   if (!file) {
      //kj_push_stringf(s, "cannot open file '%s'.", filename);
      return KOJI_ERROR;
   }
   
   /* load the file */
   koji_result_t r = koji_load(state, filename, stream_read_file, file);
   
   fclose(file);
   return r;
}

KOJI_API void koji_close(koji_state_t *state)
{
   allocator_t allocator = state->vm.allocator;
   vm_deinit(&state->vm);
   kj_free(state, &allocator);
}

KOJI_API koji_result_t koji_resume(koji_state_t *state)
{
   return vm_resume(&state->vm);
}
