#include "python_api.h"

#include <math.h>
#include <string.h>
#include <dolphin/pad.h>

static float clamp_unit(float value)
{
    if (!isfinite(value) || value < 0.0F) {
        return 0.0F;
    }
    return value > 1.0F ? 1.0F : value;
}

static int8_t controller_axis(float value)
{
    return (int8_t) lrintf(clamp_unit(value) * 160.0F - 80.0F);
}

static uint8_t controller_shoulder(float value)
{
    return (uint8_t) lrintf(clamp_unit(value) * 140.0F);
}

static uint16_t controller_buttons(const MslPythonControllerPlayer* input)
{
    uint16_t buttons = 0;
    buttons |= input->A ? PAD_BUTTON_A : 0;
    buttons |= input->B ? PAD_BUTTON_B : 0;
    buttons |= input->X ? PAD_BUTTON_X : 0;
    buttons |= input->Y ? PAD_BUTTON_Y : 0;
    buttons |= input->Z ? PAD_TRIGGER_Z : 0;
    buttons |= input->L ? PAD_TRIGGER_L : 0;
    buttons |= input->R ? PAD_TRIGGER_R : 0;
    buttons |= input->D_UP ? PAD_BUTTON_UP : 0;
    return buttons;
}

int msl_python_controller_inputs(const MslPythonControllerInput* source,
                                 size_t source_stride, MslCoreInput* output,
                                 size_t output_stride, uint32_t count)
{
    uint32_t row;
    if (source == NULL || output == NULL ||
        source_stride < sizeof(*source) || output_stride < sizeof(*output))
    {
        return -1;
    }
    for (row = 0; row < count; ++row) {
        const MslPythonControllerInput* input =
            (const void*) ((const uint8_t*) source + row * source_stride);
        MslCoreInput* result =
            (void*) ((uint8_t*) output + row * output_stride);
        int player;
        memset(result, 0, sizeof(*result));
        for (player = 0; player < MSL_CORE_MAX_PLAYERS; ++player) {
            const MslPythonControllerPlayer* src = &input->p[player];
            MslCoreInputPlayer* dst = &result->p[player];
            dst->buttons = controller_buttons(src);
            dst->main_x = controller_axis(src->main_stick_x);
            dst->main_y = controller_axis(src->main_stick_y);
            dst->c_x = controller_axis(src->c_stick_x);
            dst->c_y = controller_axis(src->c_stick_y);
            dst->l = controller_shoulder(src->shoulder);
        }
    }
    return 0;
}

_Static_assert(sizeof(MslPythonControllerPlayer) == 28,
               "Python controller player wire size");
_Static_assert(sizeof(MslPythonControllerInput) == 112,
               "Python controller input wire size");
