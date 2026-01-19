#include "alloc.h"

#include <stdlib.h>
#include <string.h>

void* alloc_aligned_64(size_t bytes) {
  void* ptr = NULL;
  // posix_memalign requires alignment to be a power-of-two multiple of sizeof(void*)
  if (posix_memalign(&ptr, 64, bytes) != 0) {
    return NULL;
  }
  memset(ptr, 0, bytes);
  return ptr;
}
