#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Allocation stats (debug/perf guardrail; not a gameplay concept).
uint64_t msl_alloc_total_calls(void);
uint64_t msl_alloc_total_bytes(void);
void msl_alloc_reset_counters(void);

// 64B-aligned, zero-initialized allocation for hot SoA arrays.
// Returns NULL on allocation failure.
void* alloc_aligned_64(size_t bytes);

// Heap wrappers used by the core (to make allocations measurable).
void* alloc_malloc(size_t bytes);
void* alloc_calloc(size_t nmemb, size_t size);
void alloc_free(void* ptr);

#ifdef __cplusplus
}  // extern "C"
#endif
