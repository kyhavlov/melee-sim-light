#ifndef MSL_PYTHON_API_H
#define MSL_PYTHON_API_H

#include "runtime/wire.h"

#include <stddef.h>
#include <stdint.h>

#if defined(MSL_CORE_SHARED) && defined(__GNUC__)
#define MSL_PYTHON_API __attribute__((visibility("default")))
#else
#define MSL_PYTHON_API
#endif

#pragma pack(push, 1)
typedef struct MslPythonControllerPlayer {
    uint8_t A;
    uint8_t B;
    uint8_t X;
    uint8_t Y;
    uint8_t Z;
    uint8_t L;
    uint8_t R;
    uint8_t D_UP;
    float main_stick_x;
    float main_stick_y;
    float c_stick_x;
    float c_stick_y;
    float shoulder;
} MslPythonControllerPlayer;

typedef struct MslPythonControllerInput {
    MslPythonControllerPlayer p[MSL_CORE_MAX_PLAYERS];
} MslPythonControllerInput;
#pragma pack(pop)

MSL_PYTHON_API int
msl_python_controller_inputs(const MslPythonControllerInput* source,
                             size_t source_stride, MslCoreInput* output,
                             size_t output_stride, uint32_t count);

#endif
