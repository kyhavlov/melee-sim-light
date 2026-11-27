#include "regenerate.h"

#include <stdlib.h>
#include <string.h>

enum {
  MSL_SLP_RAW_BEGIN = 15,
  MSL_SLP_PAYLOADS = 0x35,
  MSL_SLP_START = 0x36,
  MSL_SLP_POST = 0x38,
  MSL_SLP_END = 0x39,
  MSL_SLP_FRAME_START = 0x3A,
  MSL_SLP_FRAME_END = 0x3C,
  MSL_SLP_INSTANCE_OFFSET = 0x51,
  MSL_SLP_STORED_OFFSET = 0x55,
  MSL_SLP_STORED_END = 0x5D,
};

typedef struct MslReplayFrame {
  size_t start;
  size_t end;
  uint8_t present;
  uint8_t fighters;
} MslReplayFrame;

static uint32_t msl_read_be32(const uint8_t* bytes) {
  return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
         (uint32_t)bytes[2] << 8 | bytes[3];
}

static void msl_write_be32(uint8_t* bytes, uint32_t value) {
  bytes[0] = (uint8_t)(value >> 24);
  bytes[1] = (uint8_t)(value >> 16);
  bytes[2] = (uint8_t)(value >> 8);
  bytes[3] = (uint8_t)value;
}

// Index the last complete attempt for each frame. The checks below ensure the
// writer and the validator use the same physical-port/follower coverage.
static const char* msl_index_replay_frames(
    const uint8_t* raw, size_t offset, size_t raw_end, const uint16_t sizes[256],
    int32_t first_frame, size_t frame_count, MslReplayFrame* frames, size_t* prefix,
    size_t* suffix) {
  MslReplayFrame* open = NULL;
  int32_t current = 0;
  for (; offset < raw_end;) {
    uint8_t command = raw[offset];
    size_t event_size = 1 + sizes[command];
    if (event_size == 1 || event_size > raw_end - offset) {
      return "invalid replay event size";
    }
    if (command == MSL_SLP_FRAME_START) {
      if (open != NULL || *suffix != 0 || event_size < 5) {
        return "invalid replay frame start";
      }
      current = (int32_t)msl_read_be32(raw + offset + 1);
      int64_t index = (int64_t)current - first_frame;
      if (index < 0 || (uint64_t)index >= frame_count) {
        return "unexpected replay frame number";
      }
      if (*prefix == 0) {
        *prefix = offset;
      }
      open = &frames[index];
      *open = (MslReplayFrame){.start = offset};
    } else if (command == MSL_SLP_POST) {
      if (open == NULL || (int32_t)msl_read_be32(raw + offset + 1) != current ||
          raw[offset + 5] >= 4 || raw[offset + 6] >= 2) {
        return "invalid replay post-frame row";
      }
      unsigned slot = raw[offset + 5] + 4 * raw[offset + 6];
      if (open->present & (1U << slot)) {
        return "duplicate fighter within a replay frame";
      }
      open->present |= (uint8_t)(1U << slot);
      ++open->fighters;
    } else if (command == MSL_SLP_FRAME_END) {
      if (open == NULL || event_size < 5 ||
          (int32_t)msl_read_be32(raw + offset + 1) != current) {
        return "invalid replay frame bookend";
      }
      open->end = offset + event_size;
      open = NULL;
    } else if (command == MSL_SLP_END) {
      if (open != NULL || *prefix == 0 ||
          (*suffix != 0 && memcmp(raw + *suffix, raw + offset, event_size) != 0)) {
        return "invalid replay game end";
      }
      if (*suffix == 0) {
        *suffix = offset;
      }
    } else if (open == NULL && *prefix != 0) {
      return "unexpected event between replay frames";
    }
    offset += event_size;
  }
  if (open != NULL || *prefix == 0) {
    return "incomplete replay frame history";
  }
  if (*suffix == 0) {
    *suffix = raw_end;
  }
  return NULL;
}

PyObject* msl_regenerate_slp(const uint8_t* raw, Py_ssize_t raw_size,
                            const uint8_t* captured, int32_t first_frame, size_t frame_count) {
  static const char header[] = "{U\003raw[$U#l";
  if (raw_size < 17 || memcmp(raw, header, sizeof(header) - 1) != 0 ||
      raw[MSL_SLP_RAW_BEGIN] != MSL_SLP_PAYLOADS || raw[16] < 4 ||
      (raw[16] - 1) % 3 != 0) {
    PyErr_SetString(PyExc_ValueError, "invalid Slippi replay header");
    return NULL;
  }
  size_t raw_end = MSL_SLP_RAW_BEGIN + (uint64_t)msl_read_be32(raw + 11);
  size_t start = 16 + raw[16];
  if (raw_end > (size_t)raw_size || start >= raw_end || raw[start] != MSL_SLP_START) {
    PyErr_SetString(PyExc_ValueError, "invalid Slippi replay length or game start");
    return NULL;
  }
  uint16_t sizes[256] = {0};
  size_t post_size_offset = 0;
  for (size_t i = 17; i < start; i += 3) {
    sizes[raw[i]] = (uint16_t)((unsigned)raw[i + 1] << 8 | raw[i + 2]);
    if (raw[i] == MSL_SLP_POST) {
      post_size_offset = i + 1;
    }
  }
  size_t post_bytes = 1 + sizes[MSL_SLP_POST];
  if (post_bytes != MSL_SLP_INSTANCE_OFFSET && post_bytes != MSL_SLP_STORED_OFFSET) {
    PyErr_SetString(PyExc_ValueError, "unsupported legacy Slippi post-frame size");
    return NULL;
  }
  MslReplayFrame* frames = calloc(frame_count, sizeof(*frames));
  if (frames == NULL) {
    return PyErr_NoMemory();
  }
  size_t prefix = 0, suffix = 0, body_size = 0;
  const char* error;
  Py_BEGIN_ALLOW_THREADS
  error = msl_index_replay_frames(raw, start, raw_end, sizes, first_frame, frame_count, frames,
                                  &prefix, &suffix);
  if (error == NULL) {
    for (size_t i = 0; i < frame_count; ++i) {
      const MslReplayFrame* frame = &frames[i];
      if (frame->end == 0 || frame->present != captured[i * MSL_REGEN_FRAME_BYTES]) {
        error = "post-frame fighter coverage differs from the validated replay";
        break;
      }
      body_size += frame->end - frame->start + frame->fighters * (MSL_SLP_STORED_END - post_bytes);
    }
  }
  Py_END_ALLOW_THREADS
  if (error != NULL || body_size + prefix + raw_end - suffix - MSL_SLP_RAW_BEGIN > UINT32_MAX ||
      body_size + prefix + (size_t)raw_size - suffix > PY_SSIZE_T_MAX) {
    free(frames);
    PyErr_SetString(PyExc_ValueError, error != NULL ? error : "regenerated replay is too large");
    return NULL;
  }

  size_t output_size = prefix + body_size + (size_t)raw_size - suffix;
  PyObject* output = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)output_size);
  if (output == NULL) {
    free(frames);
    return NULL;
  }
  uint8_t* out = (uint8_t*)PyBytes_AS_STRING(output);
  Py_BEGIN_ALLOW_THREADS
  memcpy(out, raw, prefix);
  msl_write_be32(out + 11, (uint32_t)(prefix + body_size + raw_end - suffix - MSL_SLP_RAW_BEGIN));
  out[post_size_offset] = 0;
  out[post_size_offset + 1] = MSL_SLP_STORED_END - 1;
  uint8_t* dest = out + prefix;
  for (size_t i = 0; i < frame_count; ++i) {
    const uint8_t* fighter_data = captured + i * MSL_REGEN_FRAME_BYTES + 1;
    for (size_t offset = frames[i].start; offset < frames[i].end;) {
      uint8_t command = raw[offset];
      size_t event_size = 1 + sizes[command];
      memcpy(dest, raw + offset, event_size);
      dest += event_size;
      if (command == MSL_SLP_POST) {
        unsigned slot = raw[offset + 5] + 4 * raw[offset + 6];
        size_t appended = MSL_SLP_STORED_END - post_bytes;
        memcpy(dest, fighter_data + slot * MSL_REGEN_FIGHTER_BYTES +
                         MSL_REGEN_FIGHTER_BYTES - appended, appended);
        dest += appended;
      }
      offset += event_size;
    }
  }
  memcpy(dest, raw + suffix, (size_t)raw_size - suffix);
  Py_END_ALLOW_THREADS
  free(frames);
  return output;
}
