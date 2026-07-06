#pragma once

#include <stdint.h>

#include "ids.h"

// Central runtime character registry: one row per supported character. Table loaders iterate
// this instead of carrying their own fox/falco literal pairs; `name` is the data artifact stem
// (data/<family>/<name>.bin / .json). `char_id` is the Melee internal FighterKind, which is
// the id space the seeds/binding use.
// Python-side mirror: tools/extraction/char_registry.py (CHARS).
typedef struct MslCharRegistryEntry {
  uint8_t char_id;
  const char* name;
} MslCharRegistryEntry;

static const MslCharRegistryEntry MSL_CHAR_REGISTRY[] = {
    {(uint8_t)MSL_CHAR_ID_FOX, "fox"},     {(uint8_t)MSL_CHAR_ID_FALCON, "falcon"},
    {(uint8_t)MSL_CHAR_ID_SHEIK, "sheik"}, {(uint8_t)MSL_CHAR_ID_FALCO, "falco"},
    {(uint8_t)MSL_CHAR_ID_MARTH, "marth"}, {(uint8_t)MSL_CHAR_ID_ZELDA, "zelda"},
};

enum { MSL_CHAR_REGISTRY_COUNT = sizeof(MSL_CHAR_REGISTRY) / sizeof(MSL_CHAR_REGISTRY[0]) };

// Fox/Falco ("spacie") guard for the shared fox-special action-id range (341..372). Other
// characters reuse the same numeric ids for their own specials (Marth ftMs_*), so every
// MSL_ACT_FX_* comparison in shared code must be gated on this predicate.
static inline uint8_t msl_char_id_is_spacie(uint8_t char_id) {
  return (uint8_t)(char_id == (uint8_t)MSL_CHAR_ID_FOX || char_id == (uint8_t)MSL_CHAR_ID_FALCO);
}
