#include "alloc.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static _Atomic uint64_t g_alloc_calls = 0;
static _Atomic uint64_t g_alloc_bytes = 0;

uint64_t msl_alloc_total_calls(void) {
  return atomic_load_explicit(&g_alloc_calls, memory_order_relaxed);
}

uint64_t msl_alloc_total_bytes(void) {
  return atomic_load_explicit(&g_alloc_bytes, memory_order_relaxed);
}

void msl_alloc_reset_counters(void) {
  atomic_store_explicit(&g_alloc_calls, 0, memory_order_relaxed);
  atomic_store_explicit(&g_alloc_bytes, 0, memory_order_relaxed);
}

static void alloc_record(size_t bytes) {
  atomic_fetch_add_explicit(&g_alloc_calls, 1, memory_order_relaxed);
  atomic_fetch_add_explicit(&g_alloc_bytes, (uint64_t)bytes, memory_order_relaxed);
}

void* alloc_aligned_64(size_t bytes) {
  void* ptr = NULL;
  // posix_memalign requires alignment to be a power-of-two multiple of sizeof(void*)
  if (posix_memalign(&ptr, 64, bytes) != 0) {
    return NULL;
  }
  alloc_record(bytes);
  memset(ptr, 0, bytes);
  return ptr;
}

void* alloc_malloc(size_t bytes) {
  void* ptr = malloc(bytes);
  if (ptr == NULL) {
    return NULL;
  }
  alloc_record(bytes);
  memset(ptr, 0, bytes);
  return ptr;
}

void* alloc_calloc(size_t nmemb, size_t size) {
  if (nmemb == 0 || size == 0) {
    // Match calloc behavior: return either NULL or a unique pointer; not important here.
    void* ptr = calloc(nmemb, size);
    if (ptr) {
      alloc_record(0);
    }
    return ptr;
  }
  if (nmemb > (SIZE_MAX / size)) {
    return NULL;
  }
  const size_t bytes = nmemb * size;
  void* ptr = calloc(nmemb, size);
  if (ptr == NULL) {
    return NULL;
  }
  alloc_record(bytes);
  return ptr;
}

void alloc_free(void* ptr) {
  free(ptr);
}
