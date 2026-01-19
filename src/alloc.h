#pragma once

#include <stddef.h>

// 64B-aligned, zero-initialized allocation for hot SoA arrays.
// Returns NULL on allocation failure.
void* alloc_aligned_64(size_t bytes);
