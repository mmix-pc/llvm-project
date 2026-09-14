#include <stdlib.h>

int allocation_probe(size_t size) {
  void *p = malloc(size);
  void *q = calloc(size, 2);
  void *resized = realloc(p, size + 1);
  if (resized)
    p = resized;
  void *aligned = aligned_alloc(64, 128);
  void *posix = 0;
  int error = posix_memalign(&posix, 64, size);
  free(posix);
  free(aligned);
  free(q);
  free(p);
  return error;
}
