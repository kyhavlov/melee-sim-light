#ifndef MSL_BINDING_INTERNAL_H
#define MSL_BINDING_INTERNAL_H

#include "msl_py_common.h"

#include "data_dir.h"
#include "msl_taxonomy_native.h"

#include "../src/alloc.h"
#include "../src/api.h"
#include "../src/anim_frame.h"
#include "../src/anim_table.h"
#include "../src/anim_pose.h"
#include "../src/attack_id_tables.h"
#include "../src/char_params.h"
#include "../src/buttons.h"
#include "../src/common_params.h"
#include "../src/ecb_tables.h"
#include "../src/hitboxes_tables.h"
#include "../src/hurtcaps_tables.h"
#include "../src/hitlist.h"
#include "../src/input_axis.h"
#include "../src/item_article_params.h"
#include "../src/items.h"
#include "../src/ids.h"
#include "../src/move_tables.h"
#include "../src/motion_state_owners.h"
#include "../src/mpcoll_bounding.h"
#include "../src/mpcoll_end.h"
#include "../src/shield_tilt_table.h"
#include "../src/specialhi_pose.h"
#include "../src/staling.h"
#include "../src/stage_collision.h"
#include "../src/ucf.h"

#pragma pack(push, 1)
typedef struct PyMslControllerPlayer {
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
} PyMslControllerPlayer;

typedef struct PyMslControllerInput {
  PyMslControllerPlayer p[MSL_MAX_PLAYERS];
} PyMslControllerInput;
#pragma pack(pop)

enum {
  PYMSL_ACTION_FORMAT_NONE = 0,
  PYMSL_ACTION_FORMAT_RAW = 1,
  PYMSL_ACTION_FORMAT_CONTROLLER = 2,
};

typedef struct {
  MslBatch* batch;
  PyObject* match_config_obj;
  PyObject* prev_input_obj;
  PyObject* input_obj;
  PyObject* compare_obj;
  PyObject* viewpoint_obj;
  PyObject* gamestate_obj;
  PyObject* terminal_obj;
  PyObject* rollout_action_obj;
  PyObject* rollout_viewpoint_obj;
  PyObject* rollout_gamestate_obj;
  PyObject* rollout_terminal_obj;
  PyObject* rollout_done_obj;
  PyObject* rollout_reset_mask_obj;
  const uint8_t* match_config_bytes;
  size_t match_config_stride;
  const uint8_t* prev_input_bytes;
  size_t prev_input_stride;
  const uint8_t* input_bytes;
  size_t input_stride;
  uint8_t* compare_bytes;
  size_t compare_stride;
  const uint8_t* viewpoint_bytes;
  size_t viewpoint_stride;
  uint8_t* gamestate_bytes;
  size_t gamestate_stride;
  uint8_t* terminal_bytes;
  size_t terminal_stride;
  uint8_t* prev_input_storage;
  uint8_t* input_storage;
  size_t prev_input_storage_stride;
  size_t input_storage_stride;
  int rollout_horizon;
  int rollout_action_format;
  const uint8_t* rollout_action_bytes;
  size_t rollout_action_frame_stride;
  size_t rollout_action_batch_stride;
  const uint8_t* rollout_viewpoint_bytes;
  size_t rollout_viewpoint_stride;
  uint8_t* rollout_gamestate_bytes;
  size_t rollout_gamestate_frame_stride;
  size_t rollout_gamestate_batch_stride;
  uint8_t* rollout_terminal_bytes;
  size_t rollout_terminal_frame_stride;
  size_t rollout_terminal_batch_stride;
  uint8_t* rollout_done_bytes;
  size_t rollout_done_frame_stride;
  size_t rollout_done_batch_stride;
  const uint8_t* rollout_reset_mask_bytes;
  size_t rollout_reset_mask_frame_stride;
  size_t rollout_reset_mask_batch_stride;
} PyMslHandle;

PyMslHandle* unpack_handle(PyObject* handle_obj);

#endif
