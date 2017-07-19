#include "buffer.h"
#include <assert.h>
#include <stdio.h>
#include <libopencm3/cm3/cortex.h>
#include "debug.h"

/* Smallest buffers are ordered first in the list. */
static buffer_t *g_freelist;

buffer_t *buffer_allocate(size_t size)
{
  CM_ATOMIC_CONTEXT();

  buffer_t *result = NULL;
  buffer_t **tailptr = &g_freelist;
  while (*tailptr)
  {
    if ((*tailptr)->max_size >= size)
    {
      result = *tailptr;
      *tailptr = result->ptr;
      result->ptr = NULL;
      result->data_size = 0;
      break;
    }
    
    tailptr = (buffer_t**)&(*tailptr)->ptr;
  }
  
  if (!result)
  {
    warn("No buffers left, trying to allocate %d bytes", (int)size);
  }

  return result;
}

void buffer_release(buffer_t *buffer)
{
  CM_ATOMIC_CONTEXT();

  buffer->data_size = 0;
  
  buffer_t **tailptr = &g_freelist;
  while (*tailptr)
  {
    if ((*tailptr)->max_size >= buffer->max_size)
    {
      break;
    }

    tailptr = (buffer_t**)&(*tailptr)->ptr;
  }
  
  buffer->ptr = *tailptr;
  *tailptr = buffer;
}

bool buffer_printf(buffer_t* buf, const char* fmt, ...)
{
  size_t space = buf->max_size - buf->data_size;
  
  va_list ap;
  va_start(ap, fmt);
  size_t len = vsnprintf((char*)&buf->data[buf->data_size], space, fmt, ap);
  va_end(ap);
  
  if (len > space)
  {
    return false;
  }
  else
  {
    buf->data_size += len;
    return true;
  }
}

bool buffer_append(buffer_t* buf, void* data, size_t size)
{
  if (buf->data_size + size > buf->max_size)
  {
    return false;
  }
  else
  {
    memcpy(&buf->data[buf->data_size], data, size);
    buf->data_size += size;
    return true;
  }
}

void bufferlist_append(buffer_t **list, buffer_t *element)
{
  element->ptr = NULL;
  buffer_t **tailptr = list;
  while (*tailptr)
  {
    assert(*tailptr != element);
    tailptr = (buffer_t**)&(*tailptr)->ptr;
  }
  *tailptr = element;
}

buffer_t* bufferlist_popfront(buffer_t **list)
{
  if (*list)
  {
    buffer_t *result = *list;
    *list = result->ptr;
    result->ptr = NULL;
    return result;
  }
  else
  {
    return NULL;
  }
}

size_t bufferlist_size(const buffer_t *list)
{
  size_t size = 0;
  while (list)
  {
    size++;
    list = list->ptr;
  }
  return size;
}

buffer_t* buffer_slice(buffer_t* buf, size_t prefix, size_t suffix)
{
  assert(prefix >= sizeof(buffer_t));
  buffer_t *inner = (buffer_t*)&buf->data[prefix - sizeof(buffer_t)];
  inner->ptr = NULL;
  *(uint16_t*)&inner->max_size = buf->max_size - prefix - suffix;
  
  if (buf->data_size >= prefix + suffix)
  {
    inner->data_size = buf->data_size - prefix - suffix;
  }
  else
  {
    inner->data_size = 0;
  }
  
  return inner;
}

buffer_t* buffer_unslice(buffer_t* buf, size_t prefix, size_t suffix)
{
  buffer_t *outer = (buffer_t*)((char*)buf - prefix);
  assert(outer->max_size == buf->max_size + prefix + suffix);
  outer->data_size = buf->data_size + prefix + suffix;
  return outer;
}
