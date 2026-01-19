#pragma once

#include <stddef.h>

// 64B-aligned, zero-initialized allocation for hot SoA arrays.
// Returns NULL on allocation failure.
void* msl_aligned_alloc_64(size_t bytes);

