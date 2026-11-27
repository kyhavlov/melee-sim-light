#ifndef MSL_VALIDATION_REGENERATE_H
#define MSL_VALIDATION_REGENERATE_H

#include <Python.h>
#include <stdbool.h>
#include <stdint.h>

enum {
  MSL_REGEN_FIGHTERS = 8,
  MSL_REGEN_FIGHTER_BYTES = 12, // Two instance IDs and eight stored-state bytes.
  MSL_REGEN_FRAME_BYTES = 1 + MSL_REGEN_FIGHTERS * MSL_REGEN_FIGHTER_BYTES,
};

PyObject* msl_regenerate_slp(const uint8_t* raw, Py_ssize_t raw_size,
                            const uint8_t* frames, int32_t first_frame, size_t frame_count,
                            bool finalized_prefix);

#endif
