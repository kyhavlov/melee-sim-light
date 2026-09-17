#ifndef MSL_CORE_RUNTIME_BENCHMARK_WIRE_H
#define MSL_CORE_RUNTIME_BENCHMARK_WIRE_H

#include "runtime/wire.h"

#include <stdint.h>

enum {
  MSL_CORE_BENCHMARK_CASE_VERSION = 3,
};

#pragma pack(push, 1)

typedef struct MslCoreBenchmarkCaseHeader {
  uint8_t magic[8];
  uint32_t version;
  uint32_t header_size;
  uint32_t input_size;
  uint32_t frame_count;
  MslCoreMatchConfig config;
} MslCoreBenchmarkCaseHeader;

#pragma pack(pop)

static const uint8_t msl_core_benchmark_case_magic[8] = {
    'M', 'S', 'L', 'R', 'P', 'B', '0', '3',
};

_Static_assert(sizeof(MslCoreBenchmarkCaseHeader) == 24 + sizeof(MslCoreMatchConfig),
               "benchmark case header layout drift");

#endif
