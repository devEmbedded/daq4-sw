#ifndef BUFFER_H
#define BUFFER_H

#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>

typedef struct {
  void *ptr; /* Free field to use by buffer owner. */
  const uint16_t max_size;
  uint16_t data_size;
  uint8_t data[];
} __attribute__((packed)) buffer_t;

typedef void *(buffer_callback_t)();

/* Allocate a buffer with atleast the given size.
 * Returns NULL if all buffers are in use.
 * Safe to call from IRQs.
 */
buffer_t *buffer_allocate(size_t size);

/* Release a previously allocated buffer.
 * Safe to call from IRQs.
 */
void buffer_release(buffer_t *buffer);

/* Append the printf result to buffer and return true.
 * If the text doesn't fit fully in buffer, leaves buffer as is and returns false.
 */
bool buffer_printf(buffer_t *buf, const char *fmt, ...);

/* Append raw data to buffer and return true.
 * If the data doesn't fit fully, leaves buffer as is and returns false.
 */
bool buffer_append(buffer_t *buf, void *data, size_t size);

/* Linked lists of multiple buffers, uses the ptr pointer. */
void bufferlist_append(buffer_t **list, buffer_t *element);
buffer_t *bufferlist_popfront(buffer_t **list);
size_t bufferlist_size(const buffer_t *list);

/* Slicing of buffers for passing only part of it to lower levels. */
buffer_t *buffer_slice(buffer_t *buf, size_t prefix, size_t suffix);
buffer_t *buffer_unslice(buffer_t *buf, size_t prefix, size_t suffix);

#endif
