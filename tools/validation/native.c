#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "runtime/benchmark_wire.h"
#include "runtime/item_projection.h"
#include "runtime/wire.h"

extern char** environ;

// Stable Arrow C Data Interface ABI. Peppi's PyArrow StructArray exports these
// trees without materializing NumPy columns or copying any frame data.
struct ArrowArray {
  int64_t length;
  int64_t null_count;
  int64_t offset;
  int64_t n_buffers;
  int64_t n_children;
  const void** buffers;
  struct ArrowArray** children;
  struct ArrowArray* dictionary;
  void (*release)(struct ArrowArray*);
  void* private_data;
};

struct ArrowSchema {
  const char* format;
  const char* name;
  const char* metadata;
  int64_t flags;
  int64_t n_children;
  struct ArrowSchema** children;
  struct ArrowSchema* dictionary;
  void (*release)(struct ArrowSchema*);
  void* private_data;
};

typedef struct ArrowNode {
  const struct ArrowSchema* schema;
  const struct ArrowArray* array;
} ArrowNode;

typedef struct Primitive {
  const uint8_t* validity;
  const uint8_t* data;
  int64_t offset;
} Primitive;

typedef struct ReplayPlayer {
  Primitive random_seed;
  Primitive buttons;
  Primitive main_x;
  Primitive main_y;
  Primitive nml_main_x;
  Primitive nml_main_y;
  Primitive c_x;
  Primitive c_y;
  Primitive nml_c_x;
  Primitive nml_c_y;
  uint8_t cstick_from_processed;
  uint8_t main_y_from_processed;
  Primitive trigger_l;
  Primitive trigger_r;
  Primitive character;
  Primitive action;
  Primitive pos_x;
  Primitive pos_y;
  Primitive direction;
  Primitive percent;
  Primitive shield;
  Primitive stocks;
  Primitive state_age;
  Primitive airborne;
  Primitive ground;
  Primitive jumps;
  Primitive l_cancel;
  Primitive hurtbox;
  Primitive hitlag;
  Primitive misc_as;
  Primitive animation_index;
  Primitive instance_hit_by;
  Primitive instance_id;
  uint8_t instance_hit_by_present;
  uint8_t instance_id_present;
  Primitive last_attack;
  Primitive combo_count;
  Primitive last_hit_by;
  Primitive state_flags[MSL_CORE_STATE_FLAGS_BYTES];
  Primitive speed_air_x;
  Primitive speed_ground_x;
  Primitive speed_y;
  Primitive speed_x_attack;
  Primitive speed_y_attack;
} ReplayPlayer;

typedef struct ReplayItem {
  Primitive type;
  Primitive state;
  Primitive direction;
  Primitive vel_x;
  Primitive vel_y;
  Primitive pos_x;
  Primitive pos_y;
  Primitive damage;
  Primitive timer;
  Primitive spawn_id;
  Primitive misc[4];
  Primitive owner;
  Primitive instance_id;
  uint8_t instance_id_present;
} ReplayItem;

typedef struct ReplayView {
  int64_t raw_length;
  Primitive frame_id;
  Primitive frame_seed;
  const struct ArrowArray* item_list;
  ReplayItem item;
  const struct ArrowArray* fod_platform_list;
  Primitive fod_platform;
  Primitive fod_platform_height;
  const struct ArrowArray* dreamland_whispy_list;
  Primitive dreamland_whispy_direction;
  ReplayPlayer players[MSL_CORE_MAX_PLAYERS];
  int port_1based[MSL_CORE_MAX_PLAYERS];
  uint8_t team_id[MSL_CORE_MAX_PLAYERS];
  uint8_t start_stocks[MSL_CORE_MAX_PLAYERS];
  uint8_t costume_id[MSL_CORE_MAX_PLAYERS];
  uint8_t handicap[MSL_CORE_MAX_PLAYERS];
  int num_players;
  uint32_t stage_id;
  uint32_t initial_random_seed;
  uint8_t is_teams;
  uint8_t friendly_fire;
  uint8_t online_fnmsubs_zero;
  uint8_t brawl_offscreen_damage;
  uint8_t freeze_dead_up_fall_physics;
  float damage_ratio;
} ReplayView;

typedef struct FrameRows {
  int64_t* raw;
  int64_t* player_raw[MSL_CORE_MAX_PLAYERS];
  uint8_t* player_present[MSL_CORE_MAX_PLAYERS];
  int64_t count;
} FrameRows;

enum { MAX_DETAILS = 16, MAX_MISMATCH_FIELDS = 128, IO_CHUNK_BYTES = 65536 };

typedef struct MismatchDetail {
  int64_t frame;
  char field[64];
  char expected[48];
  char actual[48];
} MismatchDetail;

typedef struct MismatchField {
  char field[64];
  int64_t count;
  int64_t first_frame;
  int64_t last_frame;
} MismatchField;

typedef struct ValidationResult {
  int64_t first_mismatch_frame;
  int64_t last_mismatch_frame;
  int64_t first_render_visibility_mismatch_frame;
  int64_t matched_frames;
  int64_t mismatched_frames;
  int64_t exact_prefix_frames;
  int64_t strict_suffix_frames;
  int64_t render_visibility_mismatch_count;
  int64_t signed_zero_equal_count;
  int64_t mismatch_count;
  uint64_t mismatch_fingerprint;
  uint64_t actual_output_fingerprint;
  int detail_count;
  int mismatch_field_count;
  MismatchDetail details[MAX_DETAILS];
  MismatchField mismatch_fields[MAX_MISMATCH_FIELDS];
} ValidationResult;

typedef struct StreamState {
  const ReplayView* replay;
  const FrameRows* rows;
  int64_t compare_start_pos;
  int64_t process_end_pos;
  int64_t next_input_pos;
  int64_t output_rows;
  uint8_t write_buf[IO_CHUNK_BYTES];
  size_t write_len;
  size_t write_off;
  uint8_t output_row[sizeof(MslCoreCompare)];
  size_t output_have;
  uint8_t header_written;
  uint8_t server_protocol;
  MslCoreMatchConfig config;
  MslCoreInput previous;
  uint8_t signed_zero_equal;
  ValidationResult result;
} StreamState;

static int node_child(ArrowNode parent, const char* name, ArrowNode* out, char* error,
                      size_t error_size) {
  int64_t i;
  if (parent.schema == NULL || parent.array == NULL ||
      parent.schema->n_children != parent.array->n_children) {
    snprintf(error, error_size, "invalid Arrow node while finding %s", name);
    return -1;
  }
  for (i = 0; i < parent.schema->n_children; ++i) {
    const struct ArrowSchema* schema = parent.schema->children[i];
    if (schema != NULL && schema->name != NULL && strcmp(schema->name, name) == 0) {
      out->schema = schema;
      out->array = parent.array->children[i];
      return 0;
    }
  }
  snprintf(error, error_size, "replay Arrow field is missing: %s", name);
  return -1;
}

static int node_child_optional(ArrowNode parent, const char* name, ArrowNode* out, char* error,
                               size_t error_size) {
  int64_t i;
  if (parent.schema == NULL || parent.array == NULL ||
      parent.schema->n_children != parent.array->n_children) {
    snprintf(error, error_size, "invalid Arrow node while finding optional %s", name);
    return -1;
  }
  for (i = 0; i < parent.schema->n_children; ++i) {
    const struct ArrowSchema* schema = parent.schema->children[i];
    if (schema != NULL && schema->name != NULL && strcmp(schema->name, name) == 0) {
      out->schema = schema;
      out->array = parent.array->children[i];
      return 1;
    }
  }
  memset(out, 0, sizeof(*out));
  return 0;
}

static int primitive_from_node(ArrowNode node, const char* format, Primitive* out, char* error,
                               size_t error_size) {
  if (node.schema == NULL || node.array == NULL || node.schema->format == NULL ||
      strcmp(node.schema->format, format) != 0 || node.array->n_buffers < 2 ||
      node.array->buffers == NULL || node.array->buffers[1] == NULL ||
      (node.array->null_count != 0 && node.array->buffers[0] == NULL)) {
    snprintf(error, error_size, "unsupported Arrow primitive %s (%s)",
             node.schema != NULL && node.schema->name != NULL ? node.schema->name : "?",
             node.schema != NULL && node.schema->format != NULL ? node.schema->format : "?");
    return -1;
  }
  out->validity = (const uint8_t*)node.array->buffers[0];
  out->data = (const uint8_t*)node.array->buffers[1];
  out->offset = node.array->offset;
  return 0;
}

static int primitive_child(ArrowNode parent, const char* name, const char* format, Primitive* out,
                           char* error, size_t error_size) {
  ArrowNode child;
  if (node_child(parent, name, &child, error, error_size) != 0) {
    return -1;
  }
  return primitive_from_node(child, format, out, error, error_size);
}

static inline int8_t get_i8(const Primitive* p, int64_t i) {
  return ((const int8_t*)p->data)[p->offset + i];
}

static inline int primitive_is_valid(const Primitive* p, int64_t i) {
  int64_t bit = p->offset + i;
  return p->validity == NULL || (p->validity[bit >> 3] & (1U << (bit & 7))) != 0;
}

static inline uint8_t get_u8(const Primitive* p, int64_t i) {
  return ((const uint8_t*)p->data)[p->offset + i];
}

static inline uint16_t get_u16(const Primitive* p, int64_t i) {
  return ((const uint16_t*)(const void*)p->data)[p->offset + i];
}

static inline uint32_t get_u32(const Primitive* p, int64_t i) {
  return ((const uint32_t*)(const void*)p->data)[p->offset + i];
}

static inline int32_t get_i32(const Primitive* p, int64_t i) {
  return ((const int32_t*)(const void*)p->data)[p->offset + i];
}

static inline float get_f32(const Primitive* p, int64_t i) {
  return ((const float*)(const void*)p->data)[p->offset + i];
}

static int parse_port(PyObject* value) {
  const char* text;
  if (value == NULL || !PyUnicode_Check(value)) {
    return -1;
  }
  text = PyUnicode_AsUTF8(value);
  if (text == NULL || text[0] != 'P' || text[1] < '1' || text[1] > '4' || text[2] != '\0') {
    return -1;
  }
  return text[1] - '0';
}

static int parse_start(PyObject* start, ReplayView* replay) {
  PyObject* players;
  PyObject* value;
  PyObject* scene;
  Py_ssize_t i;
  if (!PyDict_Check(start)) {
    PyErr_SetString(PyExc_TypeError, "Peppi game.start must be a dict");
    return -1;
  }
  value = PyDict_GetItemString(start, "stage");
  if (value == NULL || !PyLong_Check(value)) {
    PyErr_SetString(PyExc_ValueError, "replay start stage is missing");
    return -1;
  }
  replay->stage_id = (uint32_t)PyLong_AsUnsignedLong(value);
  value = PyDict_GetItemString(start, "random_seed");
  if (value == NULL || !PyLong_Check(value)) {
    PyErr_SetString(PyExc_ValueError, "replay start random_seed is missing");
    return -1;
  }
  replay->initial_random_seed = (uint32_t)PyLong_AsUnsignedLong(value);
  if (PyErr_Occurred()) {
    return -1;
  }
  value = PyDict_GetItemString(start, "is_teams");
  replay->is_teams = value != NULL && PyObject_IsTrue(value) > 0;
  value = PyDict_GetItemString(start, "bitfield");
  if (value != NULL && PyList_Check(value) && PyList_GET_SIZE(value) >= 2) {
    PyObject* rules = PyList_GET_ITEM(value, 1);
    if (PyLong_Check(rules)) {
      // Slippi game-start bitfield byte 1 bit 0 is the VS rules friendly-fire
      // flag, matching gm_1601.c's rules.x1_7 publication.
      // refs/melee/src/melee/gm/gm_1601.c::gm_8016A92C
      replay->friendly_fire = (PyLong_AsUnsignedLong(rules) & 1U) != 0;
    }
  }
  scene = PyDict_GetItemString(start, "scene");
  value = scene != NULL && PyDict_Check(scene) ? PyDict_GetItemString(scene, "major") : NULL;
  if (value == NULL || !PyLong_Check(value)) {
    PyErr_SetString(PyExc_ValueError, "replay start scene major is missing");
    return -1;
  }
  // Scene major 8 proves that the Slippi online code set, including
  // BrawlOffscreenDamage, owns this match.
  // refs/slippi-ssbm-asm/Output/InjectionLists/list_netplay.json
  replay->online_fnmsubs_zero = PyLong_AsLong(value) == 8;
  replay->brawl_offscreen_damage = replay->online_fnmsubs_zero;
  replay->freeze_dead_up_fall_physics = replay->online_fnmsubs_zero;
  if (PyErr_Occurred()) {
    return -1;
  }
  value = PyDict_GetItemString(start, "damage_ratio");
  if (value == NULL) {
    PyErr_SetString(PyExc_ValueError, "replay start damage_ratio is missing");
    return -1;
  }
  replay->damage_ratio = (float)PyFloat_AsDouble(value);
  if (PyErr_Occurred()) {
    return -1;
  }
  players = PyDict_GetItemString(start, "players");
  if (players == NULL || !PyList_Check(players)) {
    PyErr_SetString(PyExc_ValueError, "replay start players must be a list");
    return -1;
  }
  for (i = 0; i < PyList_GET_SIZE(players); ++i) {
    PyObject* player = PyList_GET_ITEM(players, i);
    PyObject* type;
    PyObject* stocks_obj;
    PyObject* handicap_obj;
    int slot;
    int port;
    if (!PyDict_Check(player)) {
      continue;
    }
    type = PyDict_GetItemString(player, "type");
    if (type == NULL || !PyUnicode_Check(type) ||
        PyUnicode_CompareWithASCIIString(type, "Human") != 0) {
      continue;
    }
    port = parse_port(PyDict_GetItemString(player, "port"));
    stocks_obj = PyDict_GetItemString(player, "stocks");
    handicap_obj = PyDict_GetItemString(player, "handicap");
    if (port < 1 || stocks_obj == NULL || !PyLong_Check(stocks_obj) || handicap_obj == NULL ||
        !PyLong_Check(handicap_obj)) {
      PyErr_SetString(PyExc_ValueError, "human replay player metadata is incomplete");
      return -1;
    }
    slot = replay->num_players++;
    if (slot >= MSL_CORE_MAX_PLAYERS) {
      PyErr_SetString(PyExc_ValueError, "replay has more than four human players");
      return -1;
    }
    replay->port_1based[slot] = port;
    replay->start_stocks[slot] = (uint8_t)PyLong_AsUnsignedLong(stocks_obj);
    replay->handicap[slot] = (uint8_t)PyLong_AsUnsignedLong(handicap_obj);
    value = PyDict_GetItemString(player, "costume");
    replay->costume_id[slot] =
        value != NULL && PyLong_Check(value) ? (uint8_t)PyLong_AsUnsignedLong(value) : 0;
    replay->team_id[slot] = 0;
    if (replay->is_teams) {
      PyObject* team = PyDict_GetItemString(player, "team");
      if (team != NULL && team != Py_None && PyDict_Check(team)) {
        PyObject* color = PyDict_GetItemString(team, "color");
        if (color != NULL && PyLong_Check(color)) {
          replay->team_id[slot] = (uint8_t)PyLong_AsUnsignedLong(color);
        }
      }
    }
  }
  if (replay->num_players != 2 && replay->num_players != 4) {
    PyErr_Format(PyExc_ValueError, "expected two or four human players, got %d",
                 replay->num_players);
    return -1;
  }
  if (replay->num_players == 4 && replay->is_teams && !replay->friendly_fire) {
    PyErr_SetString(PyExc_ValueError,
                    "four-player teams validation currently requires Team Attack ON");
    return -1;
  }
  for (i = 0; i + 1 < replay->num_players; ++i) {
    Py_ssize_t j;
    for (j = i + 1; j < replay->num_players; ++j) {
      if (replay->port_1based[j] < replay->port_1based[i]) {
        int port = replay->port_1based[i];
        uint8_t stocks = replay->start_stocks[i];
        uint8_t team = replay->team_id[i];
        uint8_t costume = replay->costume_id[i];
        uint8_t handicap = replay->handicap[i];
        replay->port_1based[i] = replay->port_1based[j];
        replay->start_stocks[i] = replay->start_stocks[j];
        replay->team_id[i] = replay->team_id[j];
        replay->costume_id[i] = replay->costume_id[j];
        replay->handicap[i] = replay->handicap[j];
        replay->port_1based[j] = port;
        replay->start_stocks[j] = stocks;
        replay->team_id[j] = team;
        replay->costume_id[j] = costume;
        replay->handicap[j] = handicap;
      }
    }
  }
  return 0;
}

static int parse_metadata(PyObject* metadata, ReplayView* replay) {
  PyObject* value;
  const char* played_on;

  if (!PyDict_Check(metadata)) {
    PyErr_SetString(PyExc_TypeError, "Peppi game.metadata must be a dict");
    return -1;
  }
  value = PyDict_GetItemString(metadata, "playedOn");
  if (value == NULL || !PyUnicode_Check(value)) {
    PyErr_SetString(PyExc_ValueError, "replay metadata playedOn is missing");
    return -1;
  }
  played_on = PyUnicode_AsUTF8(value);
  if (played_on == NULL) {
    return -1;
  }

  // `playedOn` is the Slippi metadata field that owns the execution
  // environment. Slippi's Dolphin configuration uses the netplay code set for
  // netplay or other Dolphin play, including the BrawlOffscreenDamage and
  // FreezeDeadUpFallPhysics call-site patches. The offline mainline-Dolphin
  // case was independently established by bounded code probes and exact
  // replay behavior. Keep those capabilities independent of the online
  // capture's fnmsubs zero-sign behavior: offline mainline Dolphin retains
  // retail zero signs.
  // refs/slippi-ssbm-asm/README.md::Output/Netplay
  // refs/slippi-ssbm-asm/Online/Core/BrawlOffscreenDamage.asm
  if (strcmp(played_on, "dolphin") == 0 || strcmp(played_on, "mainline dolphin") == 0 ||
      strcmp(played_on, "network") == 0) {
    replay->brawl_offscreen_damage = 1;
    replay->freeze_dead_up_fall_physics = 1;
  }
  // Mainline Slippi Dolphin's JIT implements nmsub with the retail PPC
  // exact-zero sign, so scene-8 captures played on it need the retail
  // fnmsubs zero. playedOn is the discriminator, not the protocol version:
  // 3.19 "dolphin" corpus captures require the fused c - a*b zero while
  // 3.18/3.19 "mainline dolphin" captures require the retail sign (verified
  // by complete Fox/Falco aggregate streams against complete Luigi streams).
  if (strcmp(played_on, "mainline dolphin") == 0) {
    replay->online_fnmsubs_zero = 0;
  }
  return 0;
}

static int load_player(ArrowNode ports, int port_1based, ReplayPlayer* player, char* error,
                       size_t error_size) {
  char port_name[3] = {'P', (char)('0' + port_1based), '\0'};
  ArrowNode port;
  ArrowNode leader;
  ArrowNode pre;
  ArrowNode post;
  ArrowNode node;
  ArrowNode flags;
  ArrowNode velocities;
  ArrowNode raw_c_x;
  ArrowNode raw_c_y;
  ArrowNode raw_main_y;
  ArrowNode joystick;
  ArrowNode cstick;
  int has_raw_c_x;
  int has_raw_c_y;
  int k;
#define FIELD(PARENT, NAME, FORMAT, TARGET)                                               \
  do {                                                                                    \
    if (primitive_child((PARENT), (NAME), (FORMAT), &(TARGET), error, error_size) != 0) { \
      return -1;                                                                          \
    }                                                                                     \
  } while (0)
  if (node_child(ports, port_name, &port, error, error_size) != 0 ||
      node_child(port, "leader", &leader, error, error_size) != 0 ||
      node_child(leader, "pre", &pre, error, error_size) != 0 ||
      node_child(leader, "post", &post, error, error_size) != 0) {
    return -1;
  }
  FIELD(pre, "random_seed", "I", player->random_seed);
  FIELD(pre, "buttons_physical", "S", player->buttons);
  FIELD(pre, "raw_analog_x", "c", player->main_x);
  {
    int present = node_child_optional(pre, "raw_analog_y", &raw_main_y, error, error_size);
    if (present < 0) {
      return -1;
    }
    if (present != 0) {
      if (primitive_from_node(raw_main_y, "c", &player->main_y, error, error_size) != 0) {
        return -1;
      }
    } else {
      // Pre-v3.15 Slippi did not record the physical Y byte. Playback still
      // restores the normalized gameplay axis, so use its exact signed-byte
      // projection for the unavailable UCF raw lane.
      // refs/slippi-ssbm-asm/Playback/Core/RestoreGameFrame.asm
      player->main_y_from_processed = 1;
    }
  }
  // Slippi playback restores normalized gameplay axes to Fighter input state
  // and physical bytes to UCF's raw-input buffer as separate observations.
  // refs/slippi-ssbm-asm/Playback/Core/RestoreGameFrame.asm
  if (node_child(pre, "joystick", &joystick, error, error_size) != 0 ||
      primitive_child(joystick, "x", "f", &player->nml_main_x, error, error_size) != 0 ||
      primitive_child(joystick, "y", "f", &player->nml_main_y, error, error_size) != 0) {
    return -1;
  }
  has_raw_c_x = node_child_optional(pre, "raw_analog_cstick_x", &raw_c_x, error, error_size);
  has_raw_c_y = node_child_optional(pre, "raw_analog_cstick_y", &raw_c_y, error, error_size);
  if (has_raw_c_x < 0 || has_raw_c_y < 0) {
    return -1;
  }
  if (has_raw_c_x != has_raw_c_y) {
    snprintf(error, error_size, "replay has only one raw C-stick axis");
    return -1;
  }
  if (has_raw_c_x) {
    if (primitive_from_node(raw_c_x, "c", &player->c_x, error, error_size) != 0 ||
        primitive_from_node(raw_c_y, "c", &player->c_y, error, error_size) != 0) {
      return -1;
    }
  } else {
    // Pre-v3.15 Slippi event payloads do not contain the two raw C-stick
    // bytes. Their recorded processed C-stick axes are HSD's normalized
    // signed-byte values, so trunc(axis * 80) reconstructs the same input
    // admitted by replay playback without Python row materialization.
    // refs/slippi-ssbm-asm/Playback/Core/RestoreGameFrame.asm
    if (node_child(pre, "cstick", &cstick, error, error_size) != 0 ||
        primitive_child(cstick, "x", "f", &player->c_x, error, error_size) != 0 ||
        primitive_child(cstick, "y", "f", &player->c_y, error, error_size) != 0) {
      return -1;
    }
    player->cstick_from_processed = 1;
  }
  if (node_child(pre, "cstick", &cstick, error, error_size) != 0 ||
      primitive_child(cstick, "x", "f", &player->nml_c_x, error, error_size) != 0 ||
      primitive_child(cstick, "y", "f", &player->nml_c_y, error, error_size) != 0) {
    return -1;
  }
  if (node_child(pre, "triggers_physical", &node, error, error_size) != 0) {
    return -1;
  }
  FIELD(node, "l", "f", player->trigger_l);
  FIELD(node, "r", "f", player->trigger_r);

  FIELD(post, "character", "C", player->character);
  FIELD(post, "state", "S", player->action);
  if (node_child(post, "position", &node, error, error_size) != 0) {
    return -1;
  }
  FIELD(node, "x", "f", player->pos_x);
  FIELD(node, "y", "f", player->pos_y);
  FIELD(post, "direction", "f", player->direction);
  FIELD(post, "percent", "f", player->percent);
  FIELD(post, "shield", "f", player->shield);
  FIELD(post, "stocks", "C", player->stocks);
  FIELD(post, "state_age", "f", player->state_age);
  FIELD(post, "airborne", "C", player->airborne);
  FIELD(post, "ground", "S", player->ground);
  FIELD(post, "jumps", "C", player->jumps);
  FIELD(post, "l_cancel", "C", player->l_cancel);
  FIELD(post, "hurtbox_state", "C", player->hurtbox);
  FIELD(post, "hitlag", "f", player->hitlag);
  FIELD(post, "misc_as", "f", player->misc_as);
  FIELD(post, "animation_index", "I", player->animation_index);
  {
    ArrowNode value;
    int present = node_child_optional(post, "last_hit_by_instance", &value, error, error_size);
    if (present < 0) {
      return -1;
    }
    if (present != 0) {
      if (primitive_from_node(value, "S", &player->instance_hit_by, error, error_size) != 0) {
        return -1;
      }
      player->instance_hit_by_present = 1;
    }
    present = node_child_optional(post, "instance_id", &value, error, error_size);
    if (present < 0) {
      return -1;
    }
    if (present != 0) {
      if (primitive_from_node(value, "S", &player->instance_id, error, error_size) != 0) {
        return -1;
      }
      player->instance_id_present = 1;
    }
  }
  FIELD(post, "last_attack_landed", "C", player->last_attack);
  FIELD(post, "combo_count", "C", player->combo_count);
  FIELD(post, "last_hit_by", "C", player->last_hit_by);
  if (node_child(post, "state_flags", &flags, error, error_size) != 0 ||
      node_child(post, "velocities", &velocities, error, error_size) != 0) {
    return -1;
  }
  for (k = 0; k < MSL_CORE_STATE_FLAGS_BYTES; ++k) {
    char name[2] = {(char)('0' + k), '\0'};
    FIELD(flags, name, "C", player->state_flags[k]);
  }
  FIELD(velocities, "self_x_air", "f", player->speed_air_x);
  FIELD(velocities, "self_x_ground", "f", player->speed_ground_x);
  FIELD(velocities, "self_y", "f", player->speed_y);
  FIELD(velocities, "knockback_x", "f", player->speed_x_attack);
  FIELD(velocities, "knockback_y", "f", player->speed_y_attack);
#undef FIELD
  return 0;
}

static int load_items(ArrowNode items, ReplayView* replay, char* error, size_t error_size) {
  ArrowNode values;
  ArrowNode velocity;
  ArrowNode position;
  ArrowNode misc;
  int k;
#define ITEM_FIELD(PARENT, NAME, FORMAT, TARGET)                                          \
  do {                                                                                    \
    if (primitive_child((PARENT), (NAME), (FORMAT), &(TARGET), error, error_size) != 0) { \
      return -1;                                                                          \
    }                                                                                     \
  } while (0)
  if (items.schema->format == NULL || strcmp(items.schema->format, "+l") != 0 ||
      items.array->n_buffers < 2 || items.array->buffers[1] == NULL ||
      items.schema->n_children != 1 || items.array->n_children != 1 ||
      node_child(items, "item", &values, error, error_size) != 0 ||
      node_child(values, "velocity", &velocity, error, error_size) != 0 ||
      node_child(values, "position", &position, error, error_size) != 0 ||
      node_child(values, "misc", &misc, error, error_size) != 0) {
    snprintf(error, error_size, "unsupported Arrow item list");
    return -1;
  }
  replay->item_list = items.array;
  ITEM_FIELD(values, "type", "S", replay->item.type);
  ITEM_FIELD(values, "state", "C", replay->item.state);
  ITEM_FIELD(values, "direction", "f", replay->item.direction);
  ITEM_FIELD(velocity, "x", "f", replay->item.vel_x);
  ITEM_FIELD(velocity, "y", "f", replay->item.vel_y);
  ITEM_FIELD(position, "x", "f", replay->item.pos_x);
  ITEM_FIELD(position, "y", "f", replay->item.pos_y);
  ITEM_FIELD(values, "damage", "S", replay->item.damage);
  ITEM_FIELD(values, "timer", "f", replay->item.timer);
  ITEM_FIELD(values, "id", "I", replay->item.spawn_id);
  ITEM_FIELD(values, "owner", "c", replay->item.owner);
  {
    ArrowNode instance_id;
    int present = node_child_optional(values, "instance_id", &instance_id, error, error_size);
    if (present < 0) {
      return -1;
    }
    if (present != 0) {
      if (primitive_from_node(instance_id, "S", &replay->item.instance_id, error, error_size) !=
          0) {
        return -1;
      }
      replay->item.instance_id_present = 1;
    }
  }
  for (k = 0; k < 4; ++k) {
    char name[2] = {(char)('0' + k), '\0'};
    ITEM_FIELD(misc, name, "C", replay->item.misc[k]);
  }
#undef ITEM_FIELD
  return 0;
}

static int load_stage_events(ArrowNode frames, ReplayView* replay, char* error, size_t error_size) {
  ArrowNode list;
  ArrowNode values;
  int present = node_child_optional(frames, "fod_platform", &list, error, error_size);
  if (present < 0) {
    return -1;
  }
  if (present != 0) {
    if (list.schema->format == NULL || strcmp(list.schema->format, "+l") != 0 ||
        list.array->n_buffers < 2 || list.array->buffers[1] == NULL ||
        list.schema->n_children != 1 || list.array->n_children != 1 ||
        node_child(list, "fod_platform", &values, error, error_size) != 0 ||
        primitive_child(values, "platform", "C", &replay->fod_platform, error, error_size) != 0 ||
        primitive_child(values, "height", "f", &replay->fod_platform_height, error, error_size) !=
            0) {
      snprintf(error, error_size, "unsupported Arrow FoD platform list");
      return -1;
    }
    replay->fod_platform_list = list.array;
  }

  present = node_child_optional(frames, "dreamland_whispy", &list, error, error_size);
  if (present < 0) {
    return -1;
  }
  if (present != 0) {
    if (list.schema->format == NULL || strcmp(list.schema->format, "+l") != 0 ||
        list.array->n_buffers < 2 || list.array->buffers[1] == NULL ||
        list.schema->n_children != 1 || list.array->n_children != 1 ||
        node_child(list, "dreamland_whispy", &values, error, error_size) != 0 ||
        primitive_child(values, "direction", "C", &replay->dreamland_whispy_direction, error,
                        error_size) != 0) {
      snprintf(error, error_size, "unsupported Arrow Dream Land Whispy list");
      return -1;
    }
    replay->dreamland_whispy_list = list.array;
  }
  return 0;
}

static int load_replay(ArrowNode frames, ReplayView* replay, char* error, size_t error_size) {
  ArrowNode ports;
  ArrowNode start;
  ArrowNode items;
  int i;
  if (frames.schema == NULL || frames.array == NULL || frames.schema->format == NULL ||
      strcmp(frames.schema->format, "+s") != 0 || frames.array->length < 2 ||
      frames.array->null_count != 0) {
    snprintf(error, error_size, "Peppi frames must be a non-null StructArray");
    return -1;
  }
  replay->raw_length = frames.array->length;
  if (primitive_child(frames, "id", "i", &replay->frame_id, error, error_size) != 0 ||
      node_child(frames, "start", &start, error, error_size) != 0 ||
      primitive_child(start, "random_seed", "I", &replay->frame_seed, error, error_size) != 0 ||
      node_child(frames, "ports", &ports, error, error_size) != 0 ||
      node_child(frames, "item", &items, error, error_size) != 0) {
    return -1;
  }
  if (load_items(items, replay, error, error_size) != 0) {
    return -1;
  }
  if (load_stage_events(frames, replay, error, error_size) != 0) {
    return -1;
  }
  for (i = 0; i < replay->num_players; ++i) {
    if (load_player(ports, replay->port_1based[i], &replay->players[i], error, error_size) != 0) {
      return -1;
    }
  }
  return 0;
}

static int build_finalized_rows(const ReplayView* replay, FrameRows* rows, char* error,
                                size_t error_size) {
  int64_t i;
  int player;
  int32_t min_id = get_i32(&replay->frame_id, 0);
  int32_t max_id = min_id;
  int64_t range;
  int64_t* last;
  int64_t* last_player[MSL_CORE_MAX_PLAYERS] = {NULL};
  int64_t carried_player[MSL_CORE_MAX_PLAYERS] = {-1, -1, -1, -1};
  for (i = 1; i < replay->raw_length; ++i) {
    int32_t id = get_i32(&replay->frame_id, i);
    if (id < min_id) {
      min_id = id;
    }
    if (id > max_id) {
      max_id = id;
    }
  }
  range = (int64_t)max_id - (int64_t)min_id + 1;
  if (range <= 0 || range > replay->raw_length * 8 + 1024 || range > 100000000) {
    snprintf(error, error_size, "pathological replay frame-id range: %" PRId64, range);
    return -1;
  }
  last = (int64_t*)malloc((size_t)range * sizeof(*last));
  rows->raw = (int64_t*)malloc((size_t)replay->raw_length * sizeof(*rows->raw));
  for (player = 0; player < replay->num_players; ++player) {
    last_player[player] = (int64_t*)malloc((size_t)range * sizeof(*last_player[player]));
    rows->player_raw[player] =
        (int64_t*)malloc((size_t)replay->raw_length * sizeof(*rows->player_raw[player]));
    rows->player_present[player] =
        (uint8_t*)malloc((size_t)replay->raw_length * sizeof(*rows->player_present[player]));
  }
  if (last == NULL || rows->raw == NULL) {
    goto allocation_failed;
  }
  for (player = 0; player < replay->num_players; ++player) {
    if (last_player[player] == NULL || rows->player_raw[player] == NULL ||
        rows->player_present[player] == NULL) {
      goto allocation_failed;
    }
  }
  for (i = 0; i < range; ++i) {
    last[i] = -1;
    for (player = 0; player < replay->num_players; ++player) {
      last_player[player][i] = -1;
    }
  }
  for (i = 0; i < replay->raw_length; ++i) {
    int64_t key = (int64_t)get_i32(&replay->frame_id, i) - min_id;
    last[key] = i;
    for (player = 0; player < replay->num_players; ++player) {
      if (primitive_is_valid(&replay->players[player].character, i)) {
        last_player[player][key] = i;
      }
    }
  }
  for (i = 0; i < replay->raw_length; ++i) {
    int64_t key = (int64_t)get_i32(&replay->frame_id, i) - min_id;
    if (last[key] == i) {
      for (player = 0; player < replay->num_players; ++player) {
        int64_t player_row = last_player[player][key];
        if (player_row >= 0) {
          carried_player[player] = player_row;
          rows->player_present[player][rows->count] = 1;
        } else if (carried_player[player] >= 0 &&
                   get_u8(&replay->players[player].stocks, carried_player[player]) == 0) {
          // Slippi omits some pre/post events after a doubles participant has
          // been eliminated. Carry its last input only while stocks are zero,
          // and mask that player's absent comparison lanes below. An active
          // omission remains unreconstructable and is rejected.
          player_row = carried_player[player];
          rows->player_present[player][rows->count] = 0;
        } else {
          snprintf(error, error_size, "active finalized frame %d has no P%d pre/post row",
                   get_i32(&replay->frame_id, i), replay->port_1based[player]);
          goto failed;
        }
        rows->player_raw[player][rows->count] = player_row;
      }
      rows->raw[rows->count++] = i;
    }
  }
  free(last);
  for (player = 0; player < replay->num_players; ++player) {
    free(last_player[player]);
  }
  if (rows->count < 2) {
    snprintf(error, error_size, "replay has fewer than two finalized frames");
    return -1;
  }
  return 0;

allocation_failed:
  snprintf(error, error_size, "out of memory indexing replay frames");
failed:
  free(last);
  for (player = 0; player < MSL_CORE_MAX_PLAYERS; ++player) {
    free(last_player[player]);
    free(rows->player_raw[player]);
    free(rows->player_present[player]);
    rows->player_raw[player] = NULL;
    rows->player_present[player] = NULL;
  }
  free(rows->raw);
  rows->raw = NULL;
  rows->count = 0;
  return -1;
}

static uint8_t trigger_u8(float value) {
  if (!(value > 0.0F)) {
    return 0;
  }
  if (value >= 1.0F) {
    return 140;
  }
  /* Slippi records HSD_PadStatus::nml_analog{L,R}; Melee derives those
     values from the post-clamp byte with scale_analogLR == 140.
     refs/melee/src/melee/gm/gmmain.c::gmMain_8015FD24
     refs/melee/src/sysdolphin/baselib/controller.c::HSD_PadScale */
  return (uint8_t)lrintf(value * 140.0F);
}

static int8_t processed_stick_i8(float value) {
  if (value <= -1.0F) {
    return -80;
  }
  if (value >= 1.0F) {
    return 80;
  }
  return (int8_t)(value * 80.0F);
}

static void build_input(const ReplayView* replay, const FrameRows* rows, int64_t logical_pos,
                        MslCoreInput* input) {
  int player;
  memset(input, 0, sizeof(*input));
  for (player = 0; player < replay->num_players; ++player) {
    const ReplayPlayer* src = &replay->players[player];
    MslCoreInputPlayer* dst = &input->p[player];
    int64_t raw = rows->player_raw[player][logical_pos];
    dst->buttons = get_u16(&src->buttons, raw);
    dst->main_x = get_i8(&src->main_x, raw);
    dst->main_y = src->main_y_from_processed ? processed_stick_i8(get_f32(&src->nml_main_y, raw))
                                             : get_i8(&src->main_y, raw);
    if (src->cstick_from_processed) {
      dst->c_x = processed_stick_i8(get_f32(&src->c_x, raw));
      dst->c_y = processed_stick_i8(get_f32(&src->c_y, raw));
    } else {
      dst->c_x = get_i8(&src->c_x, raw);
      dst->c_y = get_i8(&src->c_y, raw);
    }
    dst->nml_main_x = processed_stick_i8(get_f32(&src->nml_main_x, raw));
    dst->nml_main_y = processed_stick_i8(get_f32(&src->nml_main_y, raw));
    dst->nml_c_x = processed_stick_i8(get_f32(&src->nml_c_x, raw));
    dst->nml_c_y = processed_stick_i8(get_f32(&src->nml_c_y, raw));
    dst->nml_valid = MSL_CORE_INPUT_NML_MAIN_VALID | MSL_CORE_INPUT_NML_C_VALID;
    dst->l = trigger_u8(get_f32(&src->trigger_l, raw));
    dst->r = trigger_u8(get_f32(&src->trigger_r, raw));
  }
}

static int build_match_config(const ReplayView* replay, const FrameRows* rows,
                              int ucf_cardinals_1_0_enabled, int ucf_shield_sdi_enabled,
                              int ucf_sdi_enabled, MslCoreMatchConfig* config, char* error,
                              size_t error_size) {
  int64_t i;
  if (rows->count == 0) {
    snprintf(error, error_size, "replay has no finalized frames");
    return -1;
  }
  if (replay->num_players != 2 && replay->num_players != 4) {
    snprintf(error, error_size, "Melee core requires two or four players, got %d",
             replay->num_players);
    return -1;
  }
  if (replay->stage_id != 2 && replay->stage_id != 3 && replay->stage_id != 8 &&
      replay->stage_id != 28 && replay->stage_id != 31 && replay->stage_id != 32) {
    snprintf(error, error_size, "Melee core requires a legal stage id (2,3,8,28,31,32), got %u",
             replay->stage_id);
    return -1;
  }
  memset(config, 0, sizeof(*config));
  config->stage_id = replay->stage_id;
  config->frame_id = get_i32(&replay->frame_id, rows->raw[0]) - 1;
  // Slippi records RNG before the frame's source callbacks. A fresh match
  // runs row 0 as its hidden warm-up and advances this seed naturally.
  // refs/slippi-ssbm-asm/Recording/SendFrameStart.s
  config->frame_pre_random_seed = get_u32(&replay->frame_seed, rows->raw[0]);
  // Game info captures the seed restored before stage construction; it is
  // distinct from online frame seeds.
  // refs/slippi-ssbm-asm/{Recording/SendGameInfo.asm,
  // Playback/Core/RestoreGameInfo.asm,Online/Core/InitOnlinePlay.asm}
  config->initial_random_seed = replay->initial_random_seed;
  config->match_damage_ratio = replay->damage_ratio;
  config->num_players = (uint8_t)replay->num_players;
  config->is_teams = replay->is_teams;
  config->friendly_fire = replay->friendly_fire;
  config->online_fnmsubs_zero = replay->online_fnmsubs_zero;
  config->brawl_offscreen_damage = replay->brawl_offscreen_damage;
  config->freeze_dead_up_fall_physics = replay->freeze_dead_up_fall_physics;
  config->ucf_cardinals_1_0_enabled = (uint8_t)ucf_cardinals_1_0_enabled;
  config->ucf_shield_sdi_enabled = (uint8_t)ucf_shield_sdi_enabled;
  config->ucf_sdi_enabled = (uint8_t)ucf_sdi_enabled;
  config->stage_event_streams = (replay->fod_platform_list != NULL ? 1U : 0U) |
                                (replay->dreamland_whispy_list != NULL ? 2U : 0U);
  for (i = 0; i < replay->num_players; ++i) {
    const ReplayPlayer* player = &replay->players[i];
    int64_t player_raw = rows->player_raw[i][0];
    uint8_t character = get_u8(&player->character, player_raw);
    uint8_t stocks = replay->start_stocks[i];
    if (character != 1 && character != 2 && character != 7 && character != 9 && character != 15 &&
        character != 17 && character != 18 && character != 19 && character != 22) {
      snprintf(error, error_size,
               "Melee core requires Fox, Captain Falcon, Sheik, Peach, Jigglypuff, Luigi, "
               "Marth, Zelda, or Falco players");
      return -1;
    }
    if (stocks > config->stock_count) {
      config->stock_count = stocks;
    }
    config->players[i].char_id = character;
    config->players[i].team_id = replay->team_id[i];
    config->players[i].costume_id = replay->costume_id[i];
    config->players[i].handicap = replay->handicap[i];
    config->players[i].facing_and_port =
        (uint8_t)((replay->port_1based[i] << 1) | (get_f32(&player->direction, player_raw) > 0.0F));
  }
  if (config->stock_count == 0) {
    config->stock_count = 1;
  }
  return 0;
}

static uint16_t frame_u16(float value) {
  if (!(value > 0.0F)) {
    return 0;
  }
  if (value >= 65535.0F) {
    return 65535;
  }
  return (uint16_t)floorf(value);
}

static int16_t frame_i16(float value) {
  if (value <= -32768.0F) {
    return -32768;
  }
  if (value >= 32767.0F) {
    return 32767;
  }
  return (int16_t)floorf(value);
}

static int64_t list_range_start(const struct ArrowArray* list, int64_t raw) {
  const int32_t* offsets = (const int32_t*)(const void*)list->buffers[1];
  return offsets[list->offset + raw];
}

static int64_t list_count(const struct ArrowArray* list, int64_t raw) {
  const int32_t* offsets = (const int32_t*)(const void*)list->buffers[1];
  int64_t i = list->offset + raw;
  return (int64_t)offsets[i + 1] - offsets[i];
}

static int64_t item_range_start(const ReplayView* replay, int64_t raw) {
  return list_range_start(replay->item_list, raw);
}

static int64_t item_count(const ReplayView* replay, int64_t raw) {
  return list_count(replay->item_list, raw);
}

static void build_expected(const ReplayView* replay, const FrameRows* rows, int64_t logical_pos,
                           MslCoreCompare* expected) {
  int player;
  int flag;
  int64_t raw = rows->raw[logical_pos];
  memset(expected, 0, sizeof(*expected));
  expected->frame_id = get_i32(&replay->frame_id, raw);
  expected->frame_pre_random_seed = get_u32(&replay->frame_seed, raw);
  expected->stage_id = replay->stage_id;
  expected->num_players = (uint8_t)replay->num_players;
  expected->is_teams = replay->is_teams;
  for (player = replay->num_players; player < MSL_CORE_MAX_PLAYERS; ++player) {
    expected->is_dead[player] = 1;
  }
  for (player = 0; player < replay->num_players; ++player) {
    const ReplayPlayer* src = &replay->players[player];
    int64_t player_raw = rows->player_raw[player][logical_pos];
    uint8_t flags3 = get_u8(&src->state_flags[3], player_raw);
    expected->team_id[player] = replay->team_id[player];
    expected->char_id[player] = get_u8(&src->character, player_raw);
    expected->pos_x[player] = get_f32(&src->pos_x, player_raw);
    expected->pos_y[player] = get_f32(&src->pos_y, player_raw);
    expected->speed_air_x_self[player] = get_f32(&src->speed_air_x, player_raw);
    expected->speed_ground_x_self[player] = get_f32(&src->speed_ground_x, player_raw);
    expected->speed_y_self[player] = get_f32(&src->speed_y, player_raw);
    expected->speed_x_attack[player] = get_f32(&src->speed_x_attack, player_raw);
    expected->speed_y_attack[player] = get_f32(&src->speed_y_attack, player_raw);
    expected->facing[player] = get_f32(&src->direction, player_raw) > 0.0F;
    expected->on_ground[player] = get_u8(&src->airborne, player_raw) == 0;
    expected->is_dead[player] = get_u8(&src->stocks, player_raw) == 0;
    expected->action_id[player] = get_u16(&src->action, player_raw);
    expected->action_frame[player] = frame_i16(get_f32(&src->state_age, player_raw));
    expected->jumps_left[player] = get_u8(&src->jumps, player_raw);
    expected->stocks[player] = get_u8(&src->stocks, player_raw);
    expected->percent[player] = get_f32(&src->percent, player_raw);
    expected->shield_hp[player] = get_f32(&src->shield, player_raw);
    expected->hitlag[player] = frame_u16(get_f32(&src->hitlag, player_raw));
    expected->hitstun[player] =
        (flags3 & 0x02U) != 0 ? frame_u16(get_f32(&src->misc_as, player_raw)) : 0;
    expected->l_cancel[player] = get_u8(&src->l_cancel, player_raw);
    expected->hurtbox_state[player] = get_u8(&src->hurtbox, player_raw);
    expected->ground_id[player] = get_u16(&src->ground, player_raw);
    expected->animation_index[player] = get_u32(&src->animation_index, player_raw);
    if (src->instance_hit_by_present) {
      expected->instance_hit_by[player] = get_u16(&src->instance_hit_by, player_raw);
    }
    if (src->instance_id_present) {
      expected->instance_id[player] = get_u16(&src->instance_id, player_raw);
    }
    expected->last_attack_landed[player] = get_u8(&src->last_attack, player_raw);
    expected->combo_count[player] = get_u8(&src->combo_count, player_raw);
    expected->last_hit_by[player] = get_u8(&src->last_hit_by, player_raw);
    for (flag = 0; flag < MSL_CORE_STATE_FLAGS_BYTES; ++flag) {
      expected->state_flags[player][flag] = get_u8(&src->state_flags[flag], player_raw);
    }
  }
  {
    int64_t count = item_count(replay, raw);
    int64_t start = item_range_start(replay, raw);
    int64_t slot;
    if (count > MSL_CORE_MAX_ITEMS) {
      count = MSL_CORE_MAX_ITEMS;
    }
    for (slot = 0; slot < count; ++slot) {
      const ReplayItem* src = &replay->item;
      MslCoreItem* dst = &expected->items[slot];
      int64_t item = start + slot;
      dst->exists = 1;
      dst->state = get_u8(&src->state, item);
      dst->type = get_u16(&src->type, item);
      dst->owner = get_i8(&src->owner, item);
      if (src->instance_id_present) {
        dst->instance_id = get_u16(&src->instance_id, item);
      }
      dst->direction = get_f32(&src->direction, item);
      dst->vel_x = get_f32(&src->vel_x, item);
      dst->vel_y = get_f32(&src->vel_y, item);
      dst->pos_x = get_f32(&src->pos_x, item);
      dst->pos_y = get_f32(&src->pos_y, item);
      dst->damage = get_u16(&src->damage, item);
      dst->timer = get_f32(&src->timer, item);
      dst->spawn_id = get_u32(&src->spawn_id, item);
      dst->misc0 = get_u8(&src->misc[0], item);
      dst->misc1 = get_u8(&src->misc[1], item);
      dst->misc2 = get_u8(&src->misc[2], item);
      dst->misc3 = get_u8(&src->misc[3], item);
    }
  }
}

typedef enum FieldKind {
  FIELD_U8,
  FIELD_U16,
  FIELD_I16,
  FIELD_U32,
  FIELD_I32,
  FIELD_F32,
} FieldKind;

typedef struct FieldSpec {
  const char* name;
  size_t offset;
  uint16_t count;
  uint8_t kind;
} FieldSpec;

#define SPEC(NAME, MEMBER, COUNT, KIND) \
  { NAME, offsetof(MslCoreCompare, MEMBER), COUNT, KIND }

static const FieldSpec compare_fields[] = {
    SPEC("frame_id", frame_id, 1, FIELD_I32),
    SPEC("frame_pre_random_seed", frame_pre_random_seed, 1, FIELD_U32),
    SPEC("stage_id", stage_id, 1, FIELD_U32),
    SPEC("num_players", num_players, 1, FIELD_U8),
    SPEC("is_teams", is_teams, 1, FIELD_U8),
    SPEC("team_id", team_id, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("char_id", char_id, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("pos_x", pos_x, MSL_CORE_MAX_PLAYERS, FIELD_F32),
    SPEC("pos_y", pos_y, MSL_CORE_MAX_PLAYERS, FIELD_F32),
    SPEC("speed_air_x_self", speed_air_x_self, MSL_CORE_MAX_PLAYERS, FIELD_F32),
    SPEC("speed_ground_x_self", speed_ground_x_self, MSL_CORE_MAX_PLAYERS, FIELD_F32),
    SPEC("speed_y_self", speed_y_self, MSL_CORE_MAX_PLAYERS, FIELD_F32),
    SPEC("speed_x_attack", speed_x_attack, MSL_CORE_MAX_PLAYERS, FIELD_F32),
    SPEC("speed_y_attack", speed_y_attack, MSL_CORE_MAX_PLAYERS, FIELD_F32),
    SPEC("facing", facing, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("on_ground", on_ground, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("is_dead", is_dead, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("action_id", action_id, MSL_CORE_MAX_PLAYERS, FIELD_U16),
    SPEC("action_frame", action_frame, MSL_CORE_MAX_PLAYERS, FIELD_I16),
    SPEC("jumps_left", jumps_left, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("stocks", stocks, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("percent", percent, MSL_CORE_MAX_PLAYERS, FIELD_F32),
    SPEC("shield_hp", shield_hp, MSL_CORE_MAX_PLAYERS, FIELD_F32),
    SPEC("hitlag", hitlag, MSL_CORE_MAX_PLAYERS, FIELD_U16),
    SPEC("hitstun", hitstun, MSL_CORE_MAX_PLAYERS, FIELD_U16),
    SPEC("l_cancel", l_cancel, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("hurtbox_state", hurtbox_state, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("ground_id", ground_id, MSL_CORE_MAX_PLAYERS, FIELD_U16),
    SPEC("animation_index", animation_index, MSL_CORE_MAX_PLAYERS, FIELD_U32),
    SPEC("instance_hit_by", instance_hit_by, MSL_CORE_MAX_PLAYERS, FIELD_U16),
    SPEC("instance_id", instance_id, MSL_CORE_MAX_PLAYERS, FIELD_U16),
    SPEC("last_attack_landed", last_attack_landed, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("combo_count", combo_count, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("last_hit_by", last_hit_by, MSL_CORE_MAX_PLAYERS, FIELD_U8),
    SPEC("state_flags", state_flags, MSL_CORE_MAX_PLAYERS* MSL_CORE_STATE_FLAGS_BYTES, FIELD_U8),
};

#undef SPEC

static size_t field_width(FieldKind kind) {
  switch (kind) {
    case FIELD_U8:
      return 1;
    case FIELD_U16:
    case FIELD_I16:
      return 2;
    case FIELD_U32:
    case FIELD_I32:
    case FIELD_F32:
      return 4;
  }
  return 0;
}

static uint32_t load_bits(const uint8_t* ptr, size_t width) {
  uint32_t value = 0;
  memcpy(&value, ptr, width);
  return value;
}

static int compare_bits_equal(FieldKind kind, uint32_t expected, uint32_t actual,
                              int signed_zero_equal) {
  if (expected == actual) {
    return 1;
  }
  return signed_zero_equal && kind == FIELD_F32 && (expected & UINT32_C(0x7FFFFFFF)) == 0 &&
         (actual & UINT32_C(0x7FFFFFFF)) == 0;
}

static void format_value(char* dst, size_t size, FieldKind kind, uint32_t bits) {
  float f;
  switch (kind) {
    case FIELD_U8:
    case FIELD_U16:
    case FIELD_U32:
      snprintf(dst, size, "%" PRIu32, bits);
      break;
    case FIELD_I16:
      snprintf(dst, size, "%" PRId16, (int16_t)bits);
      break;
    case FIELD_I32:
      snprintf(dst, size, "%" PRId32, (int32_t)bits);
      break;
    case FIELD_F32:
      memcpy(&f, &bits, sizeof(f));
      snprintf(dst, size, "%.9g (0x%08" PRIx32 ")", (double)f, bits);
      break;
  }
}

static void fingerprint_byte(ValidationResult* result, uint8_t value) {
  result->mismatch_fingerprint ^= value;
  result->mismatch_fingerprint *= UINT64_C(1099511628211);
}

static void fingerprint_u32(ValidationResult* result, uint32_t value) {
  int i;
  for (i = 0; i < 4; ++i) {
    fingerprint_byte(result, (uint8_t)(value >> (i * 8)));
  }
}

static void fingerprint_string(ValidationResult* result, const char* value) {
  do {
    fingerprint_byte(result, (uint8_t)*value);
  } while (*value++ != '\0');
}

static void fingerprint_actual_output_bytes(ValidationResult* result, const uint8_t* bytes,
                                            size_t size) {
  size_t i;
  for (i = 0; i < size; ++i) {
    result->actual_output_fingerprint ^= bytes[i];
    result->actual_output_fingerprint *= UINT64_C(1099511628211);
  }
}

static void record_detail(ValidationResult* result, int64_t frame, const char* field, int index,
                          FieldKind kind, uint32_t expected, uint32_t actual) {
  char field_label[64];
  MismatchDetail* detail;
  MismatchField* summary = NULL;
  int i;

  if (index < 0) {
    snprintf(field_label, sizeof(field_label), "%s", field);
  } else {
    snprintf(field_label, sizeof(field_label), "%s[%d]", field, index);
  }
  result->mismatch_count += 1;
  fingerprint_u32(result, (uint32_t)frame);
  fingerprint_string(result, field_label);
  fingerprint_byte(result, (uint8_t)kind);
  fingerprint_u32(result, expected);
  fingerprint_u32(result, actual);
  for (i = 0; i < result->mismatch_field_count; ++i) {
    if (strcmp(result->mismatch_fields[i].field, field_label) == 0) {
      summary = &result->mismatch_fields[i];
      break;
    }
  }
  if (summary == NULL && result->mismatch_field_count < MAX_MISMATCH_FIELDS) {
    summary = &result->mismatch_fields[result->mismatch_field_count++];
    snprintf(summary->field, sizeof(summary->field), "%s", field_label);
    summary->first_frame = frame;
  }
  if (summary != NULL) {
    summary->count += 1;
    summary->last_frame = frame;
  }
  // Keep the forensic sample useful when an early persistent field (for
  // example an instance id) differs on every later row: retain the first
  // occurrence of each distinct field instead of filling the bounded detail
  // array with repeats. Aggregate counts and fingerprints remain unchanged.
  if (summary == NULL || summary->count != 1 || result->detail_count >= MAX_DETAILS) {
    return;
  }
  detail = &result->details[result->detail_count++];
  detail->frame = frame;
  snprintf(detail->field, sizeof(detail->field), "%s", field_label);
  format_value(detail->expected, sizeof(detail->expected), kind, expected);
  format_value(detail->actual, sizeof(detail->actual), kind, actual);
}

static int actual_item_count(const MslCoreCompare* actual) {
  int count = 0;
  int i;
  for (i = 0; i < MSL_CORE_MAX_ITEMS; ++i) {
    count += actual->items[i].exists != 0;
  }
  return count;
}

typedef struct ItemFieldSpec {
  const char* name;
  size_t offset;
  uint8_t kind;
} ItemFieldSpec;

#define ITEM_SPEC(NAME, MEMBER, KIND) \
  { NAME, offsetof(MslCoreItem, MEMBER), KIND }

static const ItemFieldSpec item_compare_fields[] = {
    ITEM_SPEC("item.exists", exists, FIELD_U8),
    ITEM_SPEC("item.state", state, FIELD_U8),
    ITEM_SPEC("item.type", type, FIELD_U16),
    ITEM_SPEC("item.owner", owner, FIELD_U8),
    ITEM_SPEC("item.instance_id", instance_id, FIELD_U16),
    ITEM_SPEC("item.direction", direction, FIELD_F32),
    ITEM_SPEC("item.vel_x", vel_x, FIELD_F32),
    ITEM_SPEC("item.vel_y", vel_y, FIELD_F32),
    ITEM_SPEC("item.pos_x", pos_x, FIELD_F32),
    ITEM_SPEC("item.pos_y", pos_y, FIELD_F32),
    ITEM_SPEC("item.damage", damage, FIELD_U16),
    ITEM_SPEC("item.timer", timer, FIELD_F32),
    ITEM_SPEC("item.spawn_id", spawn_id, FIELD_U32),
    ITEM_SPEC("item.misc0", misc0, FIELD_U8),
    ITEM_SPEC("item.misc1", misc1, FIELD_U8),
    ITEM_SPEC("item.misc2", misc2, FIELD_U8),
    ITEM_SPEC("item.misc3", misc3, FIELD_U8),
};

#undef ITEM_SPEC

static int item_field_is_gameplay_state(const MslCoreItem* item, const ItemFieldSpec* spec) {
  int misc_index;
  if (spec->offset == offsetof(MslCoreItem, misc0)) {
    misc_index = 0;
  } else if (spec->offset == offsetof(MslCoreItem, misc1)) {
    misc_index = 1;
  } else if (spec->offset == offsetof(MslCoreItem, misc2)) {
    misc_index = 2;
  } else if (spec->offset == offsetof(MslCoreItem, misc3)) {
    misc_index = 3;
  } else {
    return 1;
  }
  return (msl_core_item_gameplay_misc_mask(item->type, item->state) & (1U << misc_index)) != 0;
}

static void fingerprint_actual_output(ValidationResult* result, const MslCoreCompare* actual) {
  MslCoreCompare canonical = *actual;
  size_t item_field;
  int slot;

  // The direct output lock is intentionally bit exact for every published
  // gameplay lane, including floating-point signed zero. A few Slippi item
  // misc bytes expose presentation pointers or unowned fixed-pool residue;
  // canonicalize only those source-proven non-gameplay lanes so the lock is
  // location independent across native processes.
  // refs/slippi-ssbm-asm/Recording/SendItemInfo.s
  for (slot = 0; slot < MSL_CORE_MAX_ITEMS; ++slot) {
    uint8_t* item_bytes = (uint8_t*)(void*)&canonical.items[slot];
    for (item_field = 0; item_field < sizeof(item_compare_fields) / sizeof(item_compare_fields[0]);
         ++item_field) {
      const ItemFieldSpec* spec = &item_compare_fields[item_field];
      if (!item_field_is_gameplay_state(&canonical.items[slot], spec)) {
        memset(item_bytes + spec->offset, 0, field_width((FieldKind)spec->kind));
      }
    }
  }
  fingerprint_actual_output_bytes(result, (const uint8_t*)(const void*)&canonical,
                                  sizeof(canonical));
}

static int compare_row(const ReplayView* replay, const FrameRows* rows, int64_t logical_pos,
                       const MslCoreCompare* actual, int signed_zero_equal,
                       ValidationResult* result) {
  MslCoreCompare expected;
  int64_t raw = rows->raw[logical_pos];
  int64_t frame = get_i32(&replay->frame_id, raw);
  size_t field_i;
  int64_t mismatch_before = result->mismatch_count;
  build_expected(replay, rows, logical_pos, &expected);
  for (field_i = 0; field_i < sizeof(compare_fields) / sizeof(compare_fields[0]); ++field_i) {
    const FieldSpec* spec = &compare_fields[field_i];
    const uint8_t* expected_bytes = (const uint8_t*)(const void*)&expected + spec->offset;
    const uint8_t* actual_bytes = (const uint8_t*)(const void*)actual + spec->offset;
    size_t width = field_width((FieldKind)spec->kind);
    int element;
    for (element = 0; element < spec->count; ++element) {
      int player = -1;
      if (spec->count == MSL_CORE_MAX_PLAYERS) {
        player = element;
      } else if (spec->offset == offsetof(MslCoreCompare, state_flags)) {
        player = element / MSL_CORE_STATE_FLAGS_BYTES;
      }
      if (player >= 0 && player < replay->num_players &&
          !rows->player_present[player][logical_pos]) {
        continue;
      }
      if (player >= 0 && player < replay->num_players &&
          ((spec->offset == offsetof(MslCoreCompare, instance_hit_by) &&
            !replay->players[player].instance_hit_by_present) ||
           (spec->offset == offsetof(MslCoreCompare, instance_id) &&
            !replay->players[player].instance_id_present))) {
        continue;
      }
      uint32_t expected_bits = load_bits(expected_bytes + (size_t)element * width, width);
      uint32_t actual_bits = load_bits(actual_bytes + (size_t)element * width, width);
      if (spec->offset == offsetof(MslCoreCompare, state_flags) &&
          element % MSL_CORE_STATE_FLAGS_BYTES == MSL_CORE_STATE_FLAGS_BYTES - 1 &&
          ((expected_bits ^ actual_bits) & 0x80U) != 0) {
        // fp+0x221F_b0 is published by the render traversal rather than the
        // gameplay scheduler. Nintendont/Slippi may record a gameplay tick
        // before this bit's next render publication, and the replay does not
        // record the pad-queue/render boundary needed to reconstruct that
        // phase. Keep the lane visible as a diagnostic while comparing every
        // gameplay-owned bit in this byte strictly.
        // refs/melee/src/melee/gm/gm_1A45.c::gm_801A4D34
        // refs/melee/src/melee/ft/fighter.c::ft_80087BAC
        result->render_visibility_mismatch_count += 1;
        if (result->first_render_visibility_mismatch_frame == INT64_MIN) {
          result->first_render_visibility_mismatch_frame = get_i32(&replay->frame_id, raw);
        }
        expected_bits &= ~0x80U;
        actual_bits &= ~0x80U;
      }
      if (!compare_bits_equal((FieldKind)spec->kind, expected_bits, actual_bits,
                              signed_zero_equal)) {
        record_detail(result, frame, spec->name, spec->count == 1 ? -1 : element,
                      (FieldKind)spec->kind, expected_bits, actual_bits);
      } else if (expected_bits != actual_bits) {
        result->signed_zero_equal_count += 1;
      }
    }
  }
  {
    int64_t expected_items = item_count(replay, raw);
    int actual_items = actual_item_count(actual);
    if (expected_items != actual_items) {
      record_detail(result, frame, "item_count", -1, FIELD_U32, (uint32_t)expected_items,
                    (uint32_t)actual_items);
    }
  }
  {
    size_t item_field;
    int slot;
    for (slot = 0; slot < MSL_CORE_MAX_ITEMS; ++slot) {
      const uint8_t* expected_item = (const uint8_t*)(const void*)&expected.items[slot];
      const uint8_t* actual_item = (const uint8_t*)(const void*)&actual->items[slot];
      for (item_field = 0;
           item_field < sizeof(item_compare_fields) / sizeof(item_compare_fields[0]);
           ++item_field) {
        const ItemFieldSpec* spec = &item_compare_fields[item_field];
        size_t width = field_width((FieldKind)spec->kind);
        uint32_t expected_bits = load_bits(expected_item + spec->offset, width);
        uint32_t actual_bits = load_bits(actual_item + spec->offset, width);
        if ((!replay->item.instance_id_present &&
             spec->offset == offsetof(MslCoreItem, instance_id)) ||
            !item_field_is_gameplay_state(&expected.items[slot], spec) ||
            (!expected.items[slot].exists &&
             actual->items[slot].type == MSL_CORE_ITEM_KIND_SHEIK_NEEDLE_THROWN &&
             !item_field_is_gameplay_state(&actual->items[slot], spec))) {
          // When a classified item-count divergence places a thrown needle
          // against an empty expected slot, its xDD4/xDD8 bytes are still
          // uninitialized fixed-pool residue rather than gameplay state.
          // Compare all owned fields for the extra needle, but do not make
          // the fingerprint depend on native arena placement.
          // refs/melee/src/melee/it/{itCharItems.h,
          //   items/itseakneedlethrown.c::it_802AFD8C}
          continue;
        }
        if (!compare_bits_equal((FieldKind)spec->kind, expected_bits, actual_bits,
                                signed_zero_equal)) {
          record_detail(result, frame, spec->name, slot, (FieldKind)spec->kind, expected_bits,
                        actual_bits);
        } else if (expected_bits != actual_bits) {
          result->signed_zero_equal_count += 1;
        }
      }
    }
  }
  return result->mismatch_count == mismatch_before;
}

static void refill_write_buffer(StreamState* state) {
  state->write_len = 0;
  state->write_off = 0;
  if (!state->header_written) {
    if (state->server_protocol) {
      uint32_t frame_count = (uint32_t)(state->process_end_pos + 1);
      state->write_buf[state->write_len++] = (uint8_t)frame_count;
      state->write_buf[state->write_len++] = (uint8_t)(frame_count >> 8);
      state->write_buf[state->write_len++] = (uint8_t)(frame_count >> 16);
      state->write_buf[state->write_len++] = (uint8_t)(frame_count >> 24);
    }
    memcpy(state->write_buf + state->write_len, &state->config, sizeof(state->config));
    state->write_len += sizeof(state->config);
    memcpy(state->write_buf + state->write_len, &state->previous, sizeof(state->previous));
    state->write_len += sizeof(state->previous);
    state->header_written = 1;
  }
  while (state->next_input_pos <= state->process_end_pos &&
         state->write_len + sizeof(MslCoreStreamFrame) <= sizeof(state->write_buf)) {
    MslCoreStreamFrame frame;
    int64_t logical_pos = state->next_input_pos++;
    int64_t raw = state->rows->raw[logical_pos];
    int64_t event;
    int seed_player = 0;
    memset(&frame, 0, sizeof(frame));
    while (seed_player < state->replay->num_players &&
           !state->rows->player_present[seed_player][logical_pos]) {
      ++seed_player;
    }
    if (seed_player == state->replay->num_players) {
      seed_player = 0;
    }
    frame.frame_pre_random_seed = get_u32(&state->replay->frame_seed, raw);
    frame.stage_events.fighter_pre_random_seed =
        get_u32(&state->replay->players[seed_player].random_seed,
                state->rows->player_raw[seed_player][logical_pos]);
    frame.stage_events.fighter_pre_random_seed_valid = 1;
    build_input(state->replay, state->rows, logical_pos, &frame.input);
    if (state->replay->fod_platform_list != NULL) {
      int64_t start = list_range_start(state->replay->fod_platform_list, raw);
      int64_t count = list_count(state->replay->fod_platform_list, raw);
      for (event = 0; event < count; ++event) {
        int64_t index = start + event;
        uint8_t platform = get_u8(&state->replay->fod_platform, index);
        if (platform < 2) {
          frame.stage_events.fod_platform_height[platform] =
              get_f32(&state->replay->fod_platform_height, index);
          frame.stage_events.fod_platform_mask |= (uint8_t)(1U << platform);
        }
      }
    }
    if (state->replay->dreamland_whispy_list != NULL) {
      int64_t start = list_range_start(state->replay->dreamland_whispy_list, raw);
      int64_t count = list_count(state->replay->dreamland_whispy_list, raw);
      if (count != 0) {
        frame.stage_events.dreamland_whispy_direction =
            get_u8(&state->replay->dreamland_whispy_direction, start + count - 1);
        frame.stage_events.dreamland_whispy_valid = 1;
      }
    }
    memcpy(state->write_buf + state->write_len, &frame, sizeof(frame));
    state->write_len += sizeof(frame);
  }
}

static int consume_output(StreamState* state, const uint8_t* data, size_t size, char* error,
                          size_t error_size) {
  while (size != 0) {
    size_t remaining = sizeof(state->output_row) - state->output_have;
    size_t take = size < remaining ? size : remaining;
    memcpy(state->output_row + state->output_have, data, take);
    state->output_have += take;
    data += take;
    size -= take;
    if (state->output_have == sizeof(state->output_row)) {
      int64_t logical_pos = state->output_rows++;
      if (logical_pos > state->process_end_pos) {
        snprintf(error, error_size, "core runner produced too many rows");
        return -1;
      }
      if (logical_pos >= state->compare_start_pos) {
        int64_t raw = state->rows->raw[logical_pos];
        int64_t frame = get_i32(&state->replay->frame_id, raw);
        const MslCoreCompare* actual = (const MslCoreCompare*)(const void*)state->output_row;
        fingerprint_actual_output(&state->result, actual);
        if (compare_row(state->replay, state->rows, logical_pos, actual, state->signed_zero_equal,
                        &state->result)) {
          state->result.matched_frames += 1;
          state->result.strict_suffix_frames += 1;
          if (state->result.first_mismatch_frame == INT64_MIN) {
            state->result.exact_prefix_frames += 1;
          }
        } else {
          state->result.mismatched_frames += 1;
          state->result.strict_suffix_frames = 0;
          state->result.last_mismatch_frame = frame;
          if (state->result.first_mismatch_frame == INT64_MIN) {
            state->result.first_mismatch_frame = frame;
          }
        }
      }
      state->output_have = 0;
    }
  }
  return 0;
}

static double monotonic_seconds(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return 0.0;
  }
  return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

static int set_nonblocking(int fd, char* error, size_t error_size) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
    snprintf(error, error_size, "fcntl(O_NONBLOCK): %s", strerror(errno));
    return -1;
  }
  return 0;
}

static int stream_runner_session(int input_fd, int output_fd, double timeout, StreamState* state,
                                 char* error, size_t error_size) {
  int64_t expected_rows = state->process_end_pos + 1;
  int input_complete = 0;
  double deadline = monotonic_seconds() + timeout;
  uint8_t read_buf[IO_CHUNK_BYTES];

  if (input_fd < 0 || output_fd < 0 || set_nonblocking(input_fd, error, error_size) != 0 ||
      set_nonblocking(output_fd, error, error_size) != 0) {
    return -1;
  }
  state->server_protocol = 1;
  refill_write_buffer(state);
  while (state->output_rows < expected_rows) {
    struct pollfd fds[2];
    nfds_t count = 0;
    int input_index = -1;
    int output_index;
    double remaining;
    int wait_ms;
    int polled;

    if (!input_complete && state->write_off == state->write_len) {
      refill_write_buffer(state);
      input_complete = state->write_off == state->write_len;
    }
    remaining = deadline - monotonic_seconds();
    if (remaining <= 0.0) {
      snprintf(error, error_size, "core validation timed out after %.1f seconds", timeout);
      return -1;
    }
    wait_ms = remaining >= 1.0 ? 1000 : (int)ceil(remaining * 1000.0);
    if (!input_complete) {
      input_index = (int)count;
      fds[count].fd = input_fd;
      fds[count].events = POLLOUT;
      fds[count].revents = 0;
      ++count;
    }
    output_index = (int)count;
    fds[count].fd = output_fd;
    fds[count].events = POLLIN;
    fds[count].revents = 0;
    ++count;
    polled = poll(fds, count, wait_ms);
    if (polled < 0) {
      if (errno == EINTR) {
        continue;
      }
      snprintf(error, error_size, "poll persistent core runner: %s", strerror(errno));
      return -1;
    }
    if (polled == 0) {
      continue;
    }
    if (input_index >= 0 && (fds[input_index].revents & (POLLOUT | POLLHUP | POLLERR)) != 0) {
      ssize_t wrote =
          write(input_fd, state->write_buf + state->write_off, state->write_len - state->write_off);
      if (wrote > 0) {
        state->write_off += (size_t)wrote;
        if (state->write_off == state->write_len) {
          refill_write_buffer(state);
          input_complete = state->write_off == state->write_len;
        }
      } else if (wrote < 0 && errno != EAGAIN && errno != EINTR) {
        snprintf(error, error_size, "write to persistent core runner: %s", strerror(errno));
        return -1;
      }
    }
    if ((fds[output_index].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      for (;;) {
        ssize_t got = read(output_fd, read_buf, sizeof(read_buf));
        if (got > 0) {
          if (consume_output(state, read_buf, (size_t)got, error, error_size) != 0) {
            return -1;
          }
          continue;
        }
        if (got == 0) {
          snprintf(error, error_size, "persistent core runner closed its output");
          return -1;
        }
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN) {
          break;
        }
        snprintf(error, error_size, "read from persistent core runner: %s", strerror(errno));
        return -1;
      }
    }
  }
  if (!input_complete || state->output_have != 0 || state->output_rows != expected_rows) {
    snprintf(error, error_size,
             "persistent core runner returned %" PRId64 "/%" PRId64 " rows with %zu trailing bytes",
             state->output_rows, expected_rows, state->output_have);
    return -1;
  }
  return 0;
}

static int stream_runner(const char* qemu_path, const char* sysroot, const char* binary_path,
                         const char* data_root, int direct_native, double timeout,
                         StreamState* state, char* error, size_t error_size) {
  int input_pipe[2] = {-1, -1};
  int output_pipe[2] = {-1, -1};
  int input_fd = -1;
  int output_fd = -1;
  int status = 0;
  int result = -1;
  pid_t child = -1;
  double deadline = monotonic_seconds() + timeout;
  uint8_t read_buf[IO_CHUNK_BYTES];
  posix_spawn_file_actions_t actions;
  int actions_initialized = 0;
  int spawn_error;

  // Replay validation fans out from Python threads. O_CLOEXEC must be installed atomically with
  // pipe creation so a concurrently spawned runner cannot retain another runner's pipe endpoints
  // across exec and indefinitely suppress EOF.
  if (pipe2(input_pipe, O_CLOEXEC) != 0 || pipe2(output_pipe, O_CLOEXEC) != 0) {
    snprintf(error, error_size, "pipe: %s", strerror(errno));
    goto done;
  }
  spawn_error = posix_spawn_file_actions_init(&actions);
  if (spawn_error != 0) {
    snprintf(error, error_size, "posix_spawn_file_actions_init: %s", strerror(spawn_error));
    goto done;
  }
  actions_initialized = 1;
  if ((spawn_error = posix_spawn_file_actions_adddup2(&actions, input_pipe[0], STDIN_FILENO)) !=
          0 ||
      (spawn_error = posix_spawn_file_actions_adddup2(&actions, output_pipe[1], STDOUT_FILENO)) !=
          0 ||
      (spawn_error = posix_spawn_file_actions_addclose(&actions, input_pipe[0])) != 0 ||
      (spawn_error = posix_spawn_file_actions_addclose(&actions, input_pipe[1])) != 0 ||
      (spawn_error = posix_spawn_file_actions_addclose(&actions, output_pipe[0])) != 0 ||
      (spawn_error = posix_spawn_file_actions_addclose(&actions, output_pipe[1])) != 0) {
    snprintf(error, error_size, "posix_spawn file action: %s", strerror(spawn_error));
    goto done;
  }
  if (direct_native) {
    char* const argv[] = {(char*)binary_path, (char*)data_root, "--stream", NULL};
    spawn_error = posix_spawn(&child, binary_path, &actions, NULL, argv, environ);
  } else {
    char* const argv[] = {
        (char*)qemu_path, "-L", (char*)sysroot, (char*)binary_path, (char*)data_root,
        "--stream",       NULL};
    spawn_error = posix_spawn(&child, qemu_path, &actions, NULL, argv, environ);
  }
  if (spawn_error != 0) {
    child = -1;
    snprintf(error, error_size, "posix_spawn %s: %s", direct_native ? binary_path : qemu_path,
             strerror(spawn_error));
    goto done;
  }
  posix_spawn_file_actions_destroy(&actions);
  actions_initialized = 0;
  close(input_pipe[0]);
  input_pipe[0] = -1;
  close(output_pipe[1]);
  output_pipe[1] = -1;
  input_fd = input_pipe[1];
  input_pipe[1] = -1;
  output_fd = output_pipe[0];
  output_pipe[0] = -1;
  if (set_nonblocking(input_fd, error, error_size) != 0 ||
      set_nonblocking(output_fd, error, error_size) != 0) {
    goto done;
  }
  refill_write_buffer(state);

  while (output_fd >= 0) {
    struct pollfd fds[2];
    nfds_t count = 0;
    int input_index = -1;
    int output_index;
    double remaining = deadline - monotonic_seconds();
    int wait_ms;
    int polled;
    if (remaining <= 0.0) {
      snprintf(error, error_size, "core validation timed out after %.1f seconds", timeout);
      goto done;
    }
    wait_ms = remaining >= 1.0 ? 1000 : (int)ceil(remaining * 1000.0);
    if (input_fd >= 0) {
      input_index = (int)count;
      fds[count].fd = input_fd;
      fds[count].events = POLLOUT;
      fds[count].revents = 0;
      ++count;
    }
    output_index = (int)count;
    fds[count].fd = output_fd;
    fds[count].events = POLLIN;
    fds[count].revents = 0;
    ++count;
    polled = poll(fds, count, wait_ms);
    if (polled < 0) {
      if (errno == EINTR) {
        continue;
      }
      snprintf(error, error_size, "poll: %s", strerror(errno));
      goto done;
    }
    if (polled == 0) {
      continue;
    }
    if (input_index >= 0 && (fds[input_index].revents & (POLLOUT | POLLHUP | POLLERR)) != 0) {
      ssize_t wrote;
      if (state->write_off == state->write_len) {
        refill_write_buffer(state);
      }
      if (state->write_off == state->write_len) {
        close(input_fd);
        input_fd = -1;
      } else {
        wrote = write(input_fd, state->write_buf + state->write_off,
                      state->write_len - state->write_off);
        if (wrote > 0) {
          state->write_off += (size_t)wrote;
        } else if (wrote < 0 && errno != EAGAIN && errno != EINTR) {
          snprintf(error, error_size, "write to core runner: %s", strerror(errno));
          goto done;
        }
      }
    }
    if ((fds[output_index].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
      for (;;) {
        ssize_t got = read(output_fd, read_buf, sizeof(read_buf));
        if (got > 0) {
          if (consume_output(state, read_buf, (size_t)got, error, error_size) != 0) {
            goto done;
          }
          continue;
        }
        if (got == 0) {
          close(output_fd);
          output_fd = -1;
          break;
        }
        if (errno == EINTR) {
          continue;
        }
        if (errno == EAGAIN) {
          break;
        }
        snprintf(error, error_size, "read from core runner: %s", strerror(errno));
        goto done;
      }
    }
  }
  if (input_fd >= 0) {
    close(input_fd);
    input_fd = -1;
  }
  for (;;) {
    pid_t waited = waitpid(child, &status, WNOHANG);
    if (waited == child) {
      break;
    }
    if (waited < 0 && errno != EINTR) {
      snprintf(error, error_size, "waitpid: %s", strerror(errno));
      goto done;
    }
    if (deadline - monotonic_seconds() <= 0.0) {
      snprintf(error, error_size, "core validation timed out after %.1f seconds", timeout);
      goto done;
    }
    (void)poll(NULL, 0, 10);
  }
  child = -1;
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    if (WIFSIGNALED(status)) {
      snprintf(error, error_size, "core runner terminated by signal %d (%s)", WTERMSIG(status),
               strsignal(WTERMSIG(status)));
    } else {
      snprintf(error, error_size, "core runner exited with status %d",
               WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    }
    goto done;
  }
  if (state->output_have != 0 || state->output_rows != state->process_end_pos + 1) {
    snprintf(error, error_size,
             "core runner returned %" PRId64 "/%" PRId64 " complete rows and %zu trailing bytes",
             state->output_rows, state->process_end_pos, state->output_have);
    goto done;
  }
  result = 0;

done:
  if (actions_initialized) {
    posix_spawn_file_actions_destroy(&actions);
  }
  if (input_fd >= 0) {
    close(input_fd);
  }
  if (output_fd >= 0) {
    close(output_fd);
  }
  if (input_pipe[0] >= 0) {
    close(input_pipe[0]);
  }
  if (input_pipe[1] >= 0) {
    close(input_pipe[1]);
  }
  if (output_pipe[0] >= 0) {
    close(output_pipe[0]);
  }
  if (output_pipe[1] >= 0) {
    close(output_pipe[1]);
  }
  if (child > 0) {
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
  }
  return result;
}

static int dict_set_owned(PyObject* dict, const char* key, PyObject* value) {
  int result;
  if (value == NULL) {
    return -1;
  }
  result = PyDict_SetItemString(dict, key, value);
  Py_DECREF(value);
  return result;
}

static PyObject* result_object(const ReplayView* replay, const FrameRows* rows,
                               const StreamState* state, int64_t compare_count,
                               double runner_seconds) {
  PyObject* out = PyDict_New();
  PyObject* details = NULL;
  PyObject* mismatch_fields = NULL;
  int i;
  int passed = state->result.first_mismatch_frame == INT64_MIN;
  int64_t seed_raw = rows->raw[0];
  int64_t first_raw = rows->raw[state->compare_start_pos];
  if (out == NULL) {
    return NULL;
  }
#define PUT(KEY, VALUE)                             \
  do {                                              \
    if (dict_set_owned(out, (KEY), (VALUE)) != 0) { \
      goto fail;                                    \
    }                                               \
  } while (0)
  PUT("pass", PyBool_FromLong(passed));
  PUT("frames", PyLong_FromLongLong(compare_count));
  PUT("available", PyLong_FromLongLong(rows->count - state->compare_start_pos));
  PUT("processed", PyLong_FromLongLong(state->process_end_pos));
  PUT("total", PyLong_FromLongLong(rows->count - 1));
  PUT("raw_frames", PyLong_FromLongLong(replay->raw_length));
  PUT("seed_frame", PyLong_FromLong(get_i32(&replay->frame_id, seed_raw)));
  PUT("first_ref_frame", PyLong_FromLong(get_i32(&replay->frame_id, first_raw)));
  PUT("matched_frames", PyLong_FromLongLong(state->result.matched_frames));
  PUT("mismatched_frames", PyLong_FromLongLong(state->result.mismatched_frames));
  PUT("exact_prefix_frames", PyLong_FromLongLong(state->result.exact_prefix_frames));
  PUT("strict_suffix_frames", PyLong_FromLongLong(state->result.strict_suffix_frames));
  PUT("runner_seconds", PyFloat_FromDouble(runner_seconds));
  PUT("render_visibility_mismatch_count",
      PyLong_FromLongLong(state->result.render_visibility_mismatch_count));
  PUT("signed_zero_equal_count", PyLong_FromLongLong(state->result.signed_zero_equal_count));
  PUT("mismatch_count", PyLong_FromLongLong(state->result.mismatch_count));
  {
    char fingerprint[17];
    snprintf(fingerprint, sizeof(fingerprint), "%016" PRIx64,
             state->result.actual_output_fingerprint);
    PUT("actual_output_fingerprint", PyUnicode_FromString(fingerprint));
  }
  if (passed) {
    Py_INCREF(Py_None);
    PUT("first_mismatch_frame", Py_None);
    Py_INCREF(Py_None);
    PUT("last_mismatch_frame", Py_None);
    Py_INCREF(Py_None);
    PUT("mismatch_fingerprint", Py_None);
  } else {
    PUT("first_mismatch_frame", PyLong_FromLongLong(state->result.first_mismatch_frame));
    PUT("last_mismatch_frame", PyLong_FromLongLong(state->result.last_mismatch_frame));
    {
      char fingerprint[17];
      snprintf(fingerprint, sizeof(fingerprint), "%016" PRIx64, state->result.mismatch_fingerprint);
      PUT("mismatch_fingerprint", PyUnicode_FromString(fingerprint));
    }
  }
  if (state->result.first_render_visibility_mismatch_frame == INT64_MIN) {
    Py_INCREF(Py_None);
    PUT("first_render_visibility_mismatch_frame", Py_None);
  } else {
    PUT("first_render_visibility_mismatch_frame",
        PyLong_FromLongLong(state->result.first_render_visibility_mismatch_frame));
  }
  details = PyList_New(state->result.detail_count);
  if (details == NULL) {
    goto fail;
  }
  for (i = 0; i < state->result.detail_count; ++i) {
    const MismatchDetail* detail = &state->result.details[i];
    PyObject* row =
        Py_BuildValue("{s:L,s:s,s:s,s:s}", "frame", (long long)detail->frame, "field",
                      detail->field, "expected", detail->expected, "actual", detail->actual);
    if (row == NULL) {
      goto fail;
    }
    PyList_SET_ITEM(details, i, row);
  }
  if (PyDict_SetItemString(out, "details", details) != 0) {
    goto fail;
  }
  Py_DECREF(details);
  details = NULL;
  mismatch_fields = PyList_New(state->result.mismatch_field_count);
  if (mismatch_fields == NULL) {
    goto fail;
  }
  for (i = 0; i < state->result.mismatch_field_count; ++i) {
    const MismatchField* field = &state->result.mismatch_fields[i];
    PyObject* row = Py_BuildValue(
        "{s:s,s:L,s:L,s:L}", "field", field->field, "count", (long long)field->count, "first_frame",
        (long long)field->first_frame, "last_frame", (long long)field->last_frame);
    if (row == NULL) {
      goto fail;
    }
    PyList_SET_ITEM(mismatch_fields, i, row);
  }
  if (PyDict_SetItemString(out, "mismatch_fields", mismatch_fields) != 0) {
    goto fail;
  }
  Py_DECREF(mismatch_fields);
#undef PUT
  return out;

fail:
  Py_XDECREF(details);
  Py_XDECREF(mismatch_fields);
  Py_DECREF(out);
#undef PUT
  return NULL;
}

static PyObject* validate_replay(PyObject* self, PyObject* args, PyObject* kwargs) {
  static char* keywords[] = {
      "frames",
      "start",
      "metadata",
      "qemu",
      "sysroot",
      "binary",
      "data_dir",
      "start_frame",
      "frames_limit",
      "timeout",
      "signed_zero_equal",
      "native",
      "ucf_cardinals_1_0_enabled",
      "ucf_shield_sdi_enabled",
      "ucf_sdi_enabled",
      "runner_stdin",
      "runner_stdout",
      NULL,
  };
  PyObject* frames_obj;
  PyObject* start_obj;
  PyObject* metadata_obj;
  PyObject* start_frame_obj = Py_None;
  PyObject* arrow_pair = NULL;
  const char* qemu_path;
  const char* sysroot;
  const char* binary_path;
  const char* data_root;
  unsigned long long frames_limit = 0;
  double timeout = 60.0;
  int signed_zero_equal = 0;
  int direct_native = 0;
  int ucf_cardinals_1_0_enabled = 1;
  int ucf_shield_sdi_enabled = 1;
  int ucf_sdi_enabled = 1;
  int runner_stdin = -1;
  int runner_stdout = -1;
  struct ArrowSchema* schema;
  struct ArrowArray* array;
  ArrowNode frames;
  ReplayView replay;
  FrameRows rows;
  StreamState state;
  int64_t compare_start = 1;
  int64_t compare_count;
  int64_t available;
  int64_t i;
  int stream_result;
  double runner_started;
  double runner_seconds;
  char error[512] = {0};
  PyObject* result = NULL;
  (void)self;

  if (!PyArg_ParseTupleAndKeywords(
          args, kwargs, "OOOssss|OKdpppppii:validate_replay", keywords, &frames_obj, &start_obj,
          &metadata_obj, &qemu_path, &sysroot, &binary_path, &data_root, &start_frame_obj,
          &frames_limit, &timeout, &signed_zero_equal, &direct_native, &ucf_cardinals_1_0_enabled,
          &ucf_shield_sdi_enabled, &ucf_sdi_enabled, &runner_stdin, &runner_stdout)) {
    return NULL;
  }
  if (timeout <= 0.0 || !isfinite(timeout)) {
    PyErr_SetString(PyExc_ValueError, "timeout must be positive");
    return NULL;
  }
  if ((runner_stdin >= 0) != (runner_stdout >= 0) || (runner_stdin >= 0 && !direct_native)) {
    PyErr_SetString(PyExc_ValueError, "persistent runner descriptors require the native backend");
    return NULL;
  }
  memset(&replay, 0, sizeof(replay));
  memset(&rows, 0, sizeof(rows));
  memset(&state, 0, sizeof(state));
  state.signed_zero_equal = (uint8_t)signed_zero_equal;
  state.result.first_mismatch_frame = INT64_MIN;
  state.result.last_mismatch_frame = INT64_MIN;
  state.result.first_render_visibility_mismatch_frame = INT64_MIN;
  state.result.mismatch_fingerprint = UINT64_C(14695981039346656037);
  state.result.actual_output_fingerprint = UINT64_C(14695981039346656037);
  if (parse_start(start_obj, &replay) != 0 || parse_metadata(metadata_obj, &replay) != 0) {
    goto done;
  }

  arrow_pair = PyObject_CallMethod(frames_obj, "__arrow_c_array__", NULL);
  if (arrow_pair == NULL) {
    goto done;
  }
  if (!PyTuple_Check(arrow_pair) || PyTuple_GET_SIZE(arrow_pair) != 2) {
    PyErr_SetString(PyExc_TypeError, "frames.__arrow_c_array__() returned an invalid pair");
    goto done;
  }
  schema =
      (struct ArrowSchema*)PyCapsule_GetPointer(PyTuple_GET_ITEM(arrow_pair, 0), "arrow_schema");
  if (schema == NULL) {
    goto done;
  }
  array = (struct ArrowArray*)PyCapsule_GetPointer(PyTuple_GET_ITEM(arrow_pair, 1), "arrow_array");
  if (array == NULL) {
    goto done;
  }
  frames.schema = schema;
  frames.array = array;
  if (load_replay(frames, &replay, error, sizeof(error)) != 0 ||
      build_finalized_rows(&replay, &rows, error, sizeof(error)) != 0 ||
      build_match_config(&replay, &rows, ucf_cardinals_1_0_enabled, ucf_shield_sdi_enabled,
                         ucf_sdi_enabled, &state.config, error, sizeof(error)) != 0) {
    PyErr_SetString(PyExc_ValueError, error);
    goto done;
  }
  if (start_frame_obj != Py_None) {
    long long requested = PyLong_AsLongLong(start_frame_obj);
    if (requested == -1 && PyErr_Occurred()) {
      goto done;
    }
    compare_start = -1;
    for (i = 1; i < rows.count; ++i) {
      if (get_i32(&replay.frame_id, rows.raw[i]) == requested) {
        compare_start = i;
        break;
      }
    }
    if (compare_start < 0) {
      PyErr_Format(PyExc_ValueError,
                   "start_frame %lld is not present in finalized replay frames %d..%d", requested,
                   get_i32(&replay.frame_id, rows.raw[1]),
                   get_i32(&replay.frame_id, rows.raw[rows.count - 1]));
      goto done;
    }
  }
  available = rows.count - compare_start;
  compare_count = frames_limit == 0 || frames_limit > (unsigned long long)available
                      ? available
                      : (int64_t)frames_limit;
  if (compare_count <= 0) {
    PyErr_SetString(PyExc_ValueError, "no replay transitions to compare");
    goto done;
  }

  state.replay = &replay;
  state.rows = &rows;
  state.compare_start_pos = compare_start;
  state.process_end_pos = compare_start + compare_count - 1;
  // A freshly constructed source match is at the start of Slippi's first
  // finalized frame, not at its post-frame snapshot. Run row 0 as a hidden
  // warm-up so Entry timers, scheduler state, and RNG consumers reach the
  // pre-frame state for the first compared transition (row 1).
  state.next_input_pos = 0;
  build_input(&replay, &rows, 0, &state.previous);

  runner_started = monotonic_seconds();
  Py_BEGIN_ALLOW_THREADS if (runner_stdin >= 0) {
    stream_result =
        stream_runner_session(runner_stdin, runner_stdout, timeout, &state, error, sizeof(error));
  }
  else {
    stream_result = stream_runner(qemu_path, sysroot, binary_path, data_root, direct_native,
                                  timeout, &state, error, sizeof(error));
  }
  Py_END_ALLOW_THREADS runner_seconds = monotonic_seconds() - runner_started;
  if (stream_result != 0) {
    PyErr_SetString(PyExc_RuntimeError, error);
    goto done;
  }
  result = result_object(&replay, &rows, &state, compare_count, runner_seconds);

done:
  for (i = 0; i < MSL_CORE_MAX_PLAYERS; ++i) {
    free(rows.player_raw[i]);
    free(rows.player_present[i]);
  }
  free(rows.raw);
  Py_XDECREF(arrow_pair);
  return result;
}

static PyObject* write_benchmark_case(PyObject* self, PyObject* args, PyObject* kwargs) {
  static char* keywords[] = {
      "frames",
      "start",
      "metadata",
      "output",
      "ucf_cardinals_1_0_enabled",
      "ucf_shield_sdi_enabled",
      "ucf_sdi_enabled",
      NULL,
  };
  PyObject* frames_obj;
  PyObject* start_obj;
  PyObject* metadata_obj;
  PyObject* arrow_pair = NULL;
  const char* output_path;
  int ucf_cardinals_1_0_enabled = 1;
  int ucf_shield_sdi_enabled = 1;
  int ucf_sdi_enabled = 1;
  struct ArrowSchema* schema;
  struct ArrowArray* array;
  ArrowNode frames;
  ReplayView replay;
  FrameRows rows;
  MslCoreBenchmarkCaseHeader header;
  FILE* output = NULL;
  int opened_output = 0;
  int64_t i;
  char error[512] = {0};
  PyObject* result = NULL;
  (void)self;

  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOOs|ppp:write_benchmark_case", keywords,
                                   &frames_obj, &start_obj, &metadata_obj, &output_path,
                                   &ucf_cardinals_1_0_enabled, &ucf_shield_sdi_enabled,
                                   &ucf_sdi_enabled)) {
    return NULL;
  }
  memset(&replay, 0, sizeof(replay));
  memset(&rows, 0, sizeof(rows));
  if (parse_start(start_obj, &replay) != 0 || parse_metadata(metadata_obj, &replay) != 0) {
    goto done;
  }
  arrow_pair = PyObject_CallMethod(frames_obj, "__arrow_c_array__", NULL);
  if (arrow_pair == NULL) {
    goto done;
  }
  if (!PyTuple_Check(arrow_pair) || PyTuple_GET_SIZE(arrow_pair) != 2) {
    PyErr_SetString(PyExc_TypeError, "frames.__arrow_c_array__() returned an invalid pair");
    goto done;
  }
  schema =
      (struct ArrowSchema*)PyCapsule_GetPointer(PyTuple_GET_ITEM(arrow_pair, 0), "arrow_schema");
  if (schema == NULL) {
    goto done;
  }
  array = (struct ArrowArray*)PyCapsule_GetPointer(PyTuple_GET_ITEM(arrow_pair, 1), "arrow_array");
  if (array == NULL) {
    goto done;
  }
  frames.schema = schema;
  frames.array = array;
  if (load_replay(frames, &replay, error, sizeof(error)) != 0 ||
      build_finalized_rows(&replay, &rows, error, sizeof(error)) != 0 ||
      build_match_config(&replay, &rows, ucf_cardinals_1_0_enabled, ucf_shield_sdi_enabled,
                         ucf_sdi_enabled, &header.config, error, sizeof(error)) != 0) {
    PyErr_SetString(PyExc_ValueError, error);
    goto done;
  }
  if (rows.count > UINT32_MAX) {
    PyErr_SetString(PyExc_OverflowError, "benchmark replay exceeds the case format");
    goto done;
  }

  // The benchmark is an ordinary free-running workload: the replay supplies
  // controller inputs, not per-frame Slippi RNG or moving-stage authority.
  header.config.stage_event_streams = 0;
  memcpy(header.magic, msl_core_benchmark_case_magic, sizeof(header.magic));
  header.version = MSL_CORE_BENCHMARK_CASE_VERSION;
  header.header_size = sizeof(header);
  header.input_size = sizeof(MslCoreInput);
  header.frame_count = (uint32_t)rows.count;
  output = fopen(output_path, "wb");
  if (output == NULL) {
    PyErr_Format(PyExc_OSError, "cannot create benchmark case %s: %s", output_path,
                 strerror(errno));
    goto done;
  }
  opened_output = 1;
  if (fwrite(&header, sizeof(header), 1, output) != 1) {
    snprintf(error, sizeof(error), "cannot write benchmark case header: %s", strerror(errno));
    goto write_error;
  }
  for (i = 0; i < rows.count; ++i) {
    MslCoreInput input;
    build_input(&replay, &rows, i, &input);
    if (fwrite(&input, sizeof(input), 1, output) != 1) {
      snprintf(error, sizeof(error), "cannot write benchmark case input: %s", strerror(errno));
      goto write_error;
    }
  }
  if (fclose(output) != 0) {
    output = NULL;
    snprintf(error, sizeof(error), "cannot close benchmark case: %s", strerror(errno));
    goto write_error;
  }
  output = NULL;
  result = PyLong_FromLongLong(rows.count);
  goto done;

write_error:
  if (output != NULL) {
    fclose(output);
    output = NULL;
  }
  PyErr_SetString(PyExc_OSError, error);

done:
  if (output != NULL) {
    fclose(output);
  }
  if (opened_output && result == NULL) {
    unlink(output_path);
  }
  for (i = 0; i < MSL_CORE_MAX_PLAYERS; ++i) {
    free(rows.player_raw[i]);
    free(rows.player_present[i]);
  }
  free(rows.raw);
  Py_XDECREF(arrow_pair);
  return result;
}

static uint32_t read_be_u32(const uint8_t* value) {
  return ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
         ((uint32_t)value[2] << 8) | (uint32_t)value[3];
}

static PyObject* slpz_unorder_events(PyObject* self, PyObject* args) {
  Py_buffer data = {0};
  PyObject* sizes_source;
  PyObject* sizes_sequence = NULL;
  PyObject* result = NULL;
  int sizes[256];
  uint32_t counts[256] = {0};
  uint32_t written[256] = {0};
  size_t offsets[256];
  const uint8_t* input;
  const uint8_t* event_order;
  const uint8_t* payloads;
  uint8_t* output;
  size_t total_events;
  size_t reordered_offset;
  size_t payload_size = 0;
  size_t output_size;
  size_t output_offset = 0;
  size_t i;

  (void)self;
  if (!PyArg_ParseTuple(args, "y*O", &data, &sizes_source)) {
    return NULL;
  }
  sizes_sequence = PySequence_Fast(sizes_source, "sizes must be a sequence");
  if (sizes_sequence == NULL) {
    goto done;
  }
  if (PySequence_Fast_GET_SIZE(sizes_sequence) < 256) {
    PyErr_SetString(PyExc_ValueError, "sizes must contain 256 entries");
    goto done;
  }
  for (i = 0; i < 256; ++i) {
    long value = PyLong_AsLong(PySequence_Fast_GET_ITEM(sizes_sequence, (Py_ssize_t)i));
    if (PyErr_Occurred()) {
      goto done;
    }
    if (value < 0 || value > UINT16_MAX) {
      PyErr_SetString(PyExc_ValueError, "invalid event payload size");
      goto done;
    }
    sizes[i] = (int)value;
  }
  if (data.len < 4) {
    PyErr_SetString(PyExc_ValueError, "truncated reordered event stream");
    goto done;
  }

  input = data.buf;
  total_events = read_be_u32(input);
  if (total_events > SIZE_MAX - 4) {
    PyErr_SetString(PyExc_ValueError, "invalid reordered event count");
    goto done;
  }
  reordered_offset = 4 + total_events;
  if ((size_t)data.len < reordered_offset) {
    PyErr_SetString(PyExc_ValueError, "truncated reordered event order");
    goto done;
  }
  event_order = input + 4;
  for (i = 0; i < total_events; ++i) {
    counts[event_order[i]] += 1;
  }
  for (i = 0; i < 256; ++i) {
    size_t row_size = (size_t)sizes[i];
    offsets[i] = payload_size;
    if (row_size != 0 && counts[i] > (SIZE_MAX - payload_size) / row_size) {
      PyErr_SetString(PyExc_ValueError, "invalid reordered event payload size");
      goto done;
    }
    payload_size += row_size * counts[i];
  }
  if ((size_t)data.len - reordered_offset != payload_size ||
      total_events > SIZE_MAX - payload_size ||
      total_events + payload_size > (size_t)PY_SSIZE_T_MAX) {
    PyErr_SetString(PyExc_ValueError, "invalid reordered event payload size");
    goto done;
  }

  output_size = total_events + payload_size;
  result = PyBytes_FromStringAndSize(NULL, (Py_ssize_t)output_size);
  if (result == NULL) {
    goto done;
  }
  output = (uint8_t*)PyBytes_AS_STRING(result);
  payloads = input + reordered_offset;
  for (i = 0; i < total_events; ++i) {
    uint8_t command = event_order[i];
    size_t row_size = (size_t)sizes[command];
    size_t stride = counts[command];
    size_t read_offset = offsets[command] + written[command];
    size_t j;
    output[output_offset] = command;
    for (j = 0; j < row_size; ++j) {
      output[output_offset + 1 + j] = payloads[read_offset + j * stride];
    }
    written[command] += 1;
    output_offset += 1 + row_size;
  }

done:
  Py_XDECREF(sizes_sequence);
  PyBuffer_Release(&data);
  return result;
}

static PyMethodDef module_methods[] = {
    {"validate_replay", (PyCFunction)(void*)validate_replay, METH_VARARGS | METH_KEYWORDS,
     "Stream a Peppi Arrow replay through a Melee core scalar runtime."},
    {"write_benchmark_case", (PyCFunction)(void*)write_benchmark_case, METH_VARARGS | METH_KEYWORDS,
     "Write an ordinary-input benchmark tape from Peppi Arrow buffers."},
    {"slpz_unorder_events", slpz_unorder_events, METH_VARARGS,
     "Unshuffle a decompressed .slpz event stream."},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef module_definition = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "_msl_replay_validate",
    .m_doc = "Native zero-copy validation for Melee core scalar runtimes.",
    .m_size = -1,
    .m_methods = module_methods,
};

PyMODINIT_FUNC PyInit__msl_replay_validate(void) { return PyModule_Create(&module_definition); }
