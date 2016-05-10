/*
 * koji scripting language
 * Copyright (C) 2015 Canio Massimo Tristano <massimo.tristano@gmail.com>
 * This source file is part of the koji scripting language, distributed under public domain.
 * See LICENSE for further licensing information.
 */
 
 #include "kj_support.h"
 #include <malloc.h>
 
/*------------------------------------------------------------------------------------------------*/
/* allocator                                                                                      */
/*------------------------------------------------------------------------------------------------*/
void * default_malloc(void* userdata, koji_size_t size, koji_size_t alignment)
{
   (void)userdata;
   return _aligned_malloc(size, alignment);
}

void * default_realloc(void* userdata, void *ptr, koji_size_t size, koji_size_t alignment)
{
   (void)userdata;
   return _aligned_realloc(ptr, size, alignment);
}

void default_free(void* userdata, void *ptr)
{
   (void)userdata;
   _aligned_free(ptr);
}

/*------------------------------------------------------------------------------------------------*/
/* linear allocator                                                                               */
/*------------------------------------------------------------------------------------------------*/
struct linear_allocator_page {
   char*                         cursor;
   uint                          size;
   struct linear_allocator_page *next;
   char                          data[];
};

static char* align(char* ptr, uint alignment)
{
   const uintptr_t alignment_minus_one = alignment - 1;
   return (char*)(((uintptr_t)ptr + alignment_minus_one) & ~alignment_minus_one);
}

kj_intern linear_allocator_t* linear_allocator_create(allocator_t *allocator, uint size)
{
   size = size < LINEAR_ALLOCATOR_PAGE_MIN_SIZE ? LINEAR_ALLOCATOR_PAGE_MIN_SIZE : size;
   linear_allocator_t* alloc = kj_malloc(sizeof(linear_allocator_t) + size,
                                         kj_alignof(linear_allocator_t), allocator);
   alloc->size = size;
   alloc->next = NULL;
   alloc->cursor = alloc->data;

   return alloc;
}

kj_intern void linear_allocator_destroy(linear_allocator_t *alloc, allocator_t *allocator)
{
   while (alloc) {
      linear_allocator_t* temp = alloc->next;
      kj_free(alloc, allocator);
      alloc = temp;
   }
}

kj_intern void linear_allocator_reset(linear_allocator_t **alloc, allocator_t *allocator)
{
   linear_allocator_destroy((*alloc)->next, allocator);
   (*alloc)->next = NULL;
   (*alloc)->cursor = (*alloc)->data;
}

kj_intern void* linear_allocator_alloc(linear_allocator_t **alloc, allocator_t *allocator, uint size, uint alignment)
{
   char* ptr = NULL;
   if (*alloc && (ptr = align((*alloc)->cursor, alignment)) >= (*alloc)->data + (*alloc)->size) {
      linear_allocator_t* temp = linear_allocator_create(allocator, size);
      temp->next = *alloc;
      *alloc = temp;
      ptr = align(temp->cursor, alignment);
   }
   (*alloc)->cursor = ptr + size;
   return ptr;
}

/*------------------------------------------------------------------------------------------------*/
/* sequentially growable arrays                                                                   */
/*------------------------------------------------------------------------------------------------*/
   static uint next_power2(uint v)
   {
      v--;
      v |= v >> 1;
      v |= v >> 2;
      v |= v >> 4;
      v |= v >> 8;
      v |= v >> 16;
      v++;
      return v;
   }

void * array_push_seq_ex(void *pparray, uint *psize, allocator_t *alloc, uint elem_size,
                      uint elem_align, uint count)
{
   assert(count > 0);

   void **parray = (void **)pparray;
   const uint old_size = *psize;
   *psize += count;

   uint curr_capacity = old_size ? max_u(16, next_power2(old_size)) : 0;
   uint new_capacity = max_u(16, next_power2(*psize));

   if (curr_capacity < new_capacity) {
      *parray = kj_realloc(*parray, new_capacity * elem_size, elem_align, alloc);
   }

   return (char *)(*parray) + elem_size * old_size;
}

/*------------------------------------------------------------------------------------------------*/
/* dynamic arrays                                                                                 */
/*------------------------------------------------------------------------------------------------*/
kj_intern void array_reserve(void *array_, allocator_t *alloc, uint elem_size, uint elem_align,
                             uint new_capacity)
{
  void_array *array = array_;

  /* Not enough capacity? */
  if (new_capacity > array->capacity) {
    /* Calculate and set new capacity as the smallest large enough multiple of
     * 10 */
    array->capacity = max_u(new_capacity, max_u(array->capacity * 2, 10));

    /* Allocate new buffer and copy old values over */
    array->data = kj_realloc(array->data, array->capacity * elem_size, elem_align, alloc);
    
    assert(array->data != NULL);
  }
}

kj_intern void array_resize(void *array_, allocator_t *alloc, uint elem_size, uint elem_align,
                            uint new_size)
{
  void_array *array = array_;
  array_reserve(array_, alloc, elem_size, elem_align, new_size);
  array->size = new_size;
}

kj_intern void array_free(void *array_, allocator_t *alloc)
{
  void_array *array = array_;
  kj_free(array->data, alloc);
  array->capacity = 0;
  array->size = 0;
  array->data = NULL;
}

kj_intern void *_array_push(void *array_, allocator_t *alloc, uint elem_size, uint elem_align,
                            uint num_elements)
{
  void_array *array = array_;
  uint prev_size = array->size;
  array_resize(array, alloc, elem_size, elem_align, array->size + num_elements);
  return ((char *)array->data) + prev_size * elem_size;
}
