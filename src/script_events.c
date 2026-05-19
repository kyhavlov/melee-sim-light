#include "script_events.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"

enum {
  MSL_CHAR_FOX = 1,
  MSL_CHAR_FALCO = 22,
  MSLFTSC1_VERSION = 2,
  MSLFTSC1_HEADER_SIZE = 28,
  MSLFTSC1_INDEX_RECORD_SIZE = 12,
  MSLFTSC1_EVENT_HEADER_SIZE = 8,
};

typedef struct MslScriptEntry {
  uint16_t msid;
  uint32_t first_event;
  uint32_t event_count;
} MslScriptEntry;

typedef struct MslScriptTable {
  MslScriptEntry* entries;
  MslScriptEvent* events;
  uint32_t entry_count;
  uint32_t event_count;
} MslScriptTable;

static MslScriptTable g_tables[256];
static int g_loaded = 0;

static uint16_t read_le_u16(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le_u32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float read_le_f32(const uint8_t* p) {
  uint32_t u = read_le_u32(p);
  float f = 0.0f;
  memcpy(&f, &u, sizeof(f));
  return f;
}

static int script_entry_cmp_msid(const void* a, const void* b) {
  const MslScriptEntry* ea = (const MslScriptEntry*)a;
  const MslScriptEntry* eb = (const MslScriptEntry*)b;
  return (ea->msid > eb->msid) - (ea->msid < eb->msid);
}

static void script_table_free(MslScriptTable* table) {
  if (table == NULL) {
    return;
  }
  alloc_free(table->events);
  alloc_free(table->entries);
  *table = (MslScriptTable){0};
}

static int decode_event_payload(MslScriptEvent* out, const uint8_t* payload, uint32_t len) {
  if (out == NULL || (len != 0u && payload == NULL)) {
    return -1;
  }
  switch ((MslScriptEventKind)out->kind_id) {
    case MSL_SCRIPT_EVENT_CLEAR_HITBOXES:
    case MSL_SCRIPT_EVENT_ALLOW_INTERRUPT:
    case MSL_SCRIPT_EVENT_SET_THROW_SPAWN_PROJECTILE:
    case MSL_SCRIPT_EVENT_TOGGLE_BONE_PHYSICS:
      return len == 0u ? 0 : -1;
    case MSL_SCRIPT_EVENT_SET_CMD_VAR:
      if (len != 4u) {
        return -1;
      }
      out->payload.cmd_var.idx = payload[0];
      out->payload.cmd_var.value = read_le_u16(payload + 1);
      return 0;
    case MSL_SCRIPT_EVENT_SET_THROW_FLAGS:
      if (len != 4u) {
        return -1;
      }
      out->payload.throw_flags.hit_idx = payload[0];
      return 0;
    case MSL_SCRIPT_EVENT_SET_AIRBORNE_STATE:
    case MSL_SCRIPT_EVENT_SET_HIT_STATUS:
    case MSL_SCRIPT_EVENT_SET_ALL_HURT_STATE:
    case MSL_SCRIPT_EVENT_SET_JAB_RAPID:
      if (len != 4u) {
        return -1;
      }
      out->payload.state.state = payload[0];
      return 0;
    case MSL_SCRIPT_EVENT_SET_HURT_STATE:
      if (len != 4u) {
        return -1;
      }
      out->payload.hurt_state.bone_idx = payload[0];
      out->payload.hurt_state.state = payload[1];
      return 0;
    case MSL_SCRIPT_EVENT_SET_JAB_COMBO:
      if (len != 4u) {
        return -1;
      }
      out->payload.jab_combo.disabled = payload[0];
      return 0;
    case MSL_SCRIPT_EVENT_SET_STATE_FLAGS_221C_U16_Y:
      if (len != 4u) {
        return -1;
      }
      out->payload.state_flags_221c.flags = read_le_u16(payload);
      return 0;
    case MSL_SCRIPT_EVENT_START_SMASH_CHARGE:
      if (len != 8u) {
        return -1;
      }
      out->payload.smash_charge.hold_frames = payload[0];
      out->payload.smash_charge.color_anim = payload[1];
      out->payload.smash_charge.damage_mul = read_le_f32(payload + 4);
      return 0;
    case MSL_SCRIPT_EVENT_PSEUDO_RANDOM_SFX:
      if (len != 4u) {
        return -1;
      }
      out->payload.pseudo_random_sfx.random_range = payload[0];
      out->payload.pseudo_random_sfx.volume = payload[1];
      out->payload.pseudo_random_sfx.panning = payload[2];
      out->payload.pseudo_random_sfx.behavior = payload[3];
      return 0;
    case MSL_SCRIPT_EVENT_SET_THROW_HITBOX:
      if (len != 16u) {
        return -1;
      }
      out->payload.throw_hitbox.idx = payload[0];
      out->payload.throw_hitbox.element = payload[1];
      out->payload.throw_hitbox.sfx_kind = payload[2];
      out->payload.throw_hitbox.sfx_severity = payload[3];
      out->payload.throw_hitbox.angle = read_le_u16(payload + 4);
      out->payload.throw_hitbox.kbg = read_le_u16(payload + 6);
      out->payload.throw_hitbox.wsk = read_le_u16(payload + 8);
      out->payload.throw_hitbox.bkb = read_le_u16(payload + 10);
      out->payload.throw_hitbox.damage = read_le_f32(payload + 12);
      return 0;
    case MSL_SCRIPT_EVENT_CREATE_HITBOX:
      if (len != 40u) {
        return -1;
      }
      out->payload.create_hitbox.hitbox_id = payload[0];
      out->payload.create_hitbox.bone = payload[1];
      out->payload.create_hitbox.hit_group = payload[2];
      out->payload.create_hitbox.element = payload[3];
      out->payload.create_hitbox.sfx_kind = payload[4];
      out->payload.create_hitbox.sfx_severity = payload[5];
      out->payload.create_hitbox.shield_damage = (int8_t)payload[6];
      out->payload.create_hitbox.rehit_frames = payload[7];
      out->payload.create_hitbox.angle = read_le_u16(payload + 8);
      out->payload.create_hitbox.kbg = read_le_u16(payload + 10);
      out->payload.create_hitbox.wsk = read_le_u16(payload + 12);
      out->payload.create_hitbox.bkb = read_le_u16(payload + 14);
      out->payload.create_hitbox.damage = read_le_f32(payload + 16);
      out->payload.create_hitbox.size = read_le_f32(payload + 20);
      out->payload.create_hitbox.x_offset = read_le_f32(payload + 24);
      out->payload.create_hitbox.y_offset = read_le_f32(payload + 28);
      out->payload.create_hitbox.z_offset = read_le_f32(payload + 32);
      out->payload.create_hitbox.flags = read_le_u32(payload + 36);
      return 0;
    default:
      return -1;
  }
}

static int script_table_load(const char* data_dir, const char* rel_path, MslScriptTable* out) {
  if (data_dir == NULL || rel_path == NULL || out == NULL) {
    return -1;
  }
  *out = (MslScriptTable){0};

  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/%s", data_dir, rel_path);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    return -1;
  }

  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    return -1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  const long sz = ftell(f);
  if (sz < MSLFTSC1_HEADER_SIZE) {
    fclose(f);
    return -1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return -1;
  }

  uint8_t* buf = (uint8_t*)alloc_malloc((size_t)sz);
  if (buf == NULL) {
    fclose(f);
    return -1;
  }
  const size_t got = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (got != (size_t)sz) {
    alloc_free(buf);
    return -1;
  }

  const uint32_t version = read_le_u32(buf + 8);
  if (memcmp(buf, "MSLFTSC1", 8) != 0 || version != (uint32_t)MSLFTSC1_VERSION) {
    alloc_free(buf);
    return -1;
  }
  const uint32_t entry_count = read_le_u32(buf + 12);
  const uint32_t event_count = read_le_u32(buf + 16);
  const uint32_t index_off = read_le_u32(buf + 20);
  const uint32_t event_off = read_le_u32(buf + 24);
  const uint64_t expected_event_off =
      (uint64_t)MSLFTSC1_HEADER_SIZE + (uint64_t)entry_count * MSLFTSC1_INDEX_RECORD_SIZE;
  if (index_off != (uint32_t)MSLFTSC1_HEADER_SIZE || event_off != expected_event_off ||
      event_off > (uint32_t)sz) {
    alloc_free(buf);
    return -1;
  }

  MslScriptEntry* entries =
      (MslScriptEntry*)alloc_calloc((size_t)entry_count, sizeof(MslScriptEntry));
  MslScriptEvent* events =
      (MslScriptEvent*)alloc_calloc((size_t)event_count, sizeof(MslScriptEvent));
  if (entries == NULL || events == NULL) {
    alloc_free(events);
    alloc_free(entries);
    alloc_free(buf);
    return -1;
  }

  for (uint32_t i = 0; i < entry_count; i++) {
    const uint8_t* rec = buf + index_off + (size_t)i * MSLFTSC1_INDEX_RECORD_SIZE;
    entries[i].msid = read_le_u16(rec);
    entries[i].first_event = read_le_u32(rec + 4);
    entries[i].event_count = read_le_u32(rec + 8);
    if (entries[i].first_event > event_count ||
        entries[i].event_count > event_count - entries[i].first_event) {
      alloc_free(events);
      alloc_free(entries);
      alloc_free(buf);
      return -1;
    }
  }

  size_t off = (size_t)event_off;
  for (uint32_t i = 0; i < event_count; i++) {
    if (off + MSLFTSC1_EVENT_HEADER_SIZE > (size_t)sz) {
      alloc_free(events);
      alloc_free(entries);
      alloc_free(buf);
      return -1;
    }
    const uint8_t* ev = buf + off;
    const uint32_t payload_len = read_le_u32(ev + 4);
    if (payload_len > (uint32_t)((size_t)sz - off - MSLFTSC1_EVENT_HEADER_SIZE)) {
      alloc_free(events);
      alloc_free(entries);
      alloc_free(buf);
      return -1;
    }
    events[i].frame = read_le_u16(ev);
    events[i].kind_id = read_le_u16(ev + 2);
    const uint8_t* payload = ev + MSLFTSC1_EVENT_HEADER_SIZE;
    if (decode_event_payload(&events[i], payload, payload_len) != 0) {
      alloc_free(events);
      alloc_free(entries);
      alloc_free(buf);
      return -1;
    }
    off += MSLFTSC1_EVENT_HEADER_SIZE + (size_t)payload_len;
  }
  if (off != (size_t)sz) {
    alloc_free(events);
    alloc_free(entries);
    alloc_free(buf);
    return -1;
  }

  qsort(entries, (size_t)entry_count, sizeof(entries[0]), script_entry_cmp_msid);
  out->entries = entries;
  out->events = events;
  out->entry_count = entry_count;
  out->event_count = event_count;
  alloc_free(buf);
  return 0;
}

int script_events_init(void) {
  if (g_loaded) {
    return 0;
  }
  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }
  if (script_table_load(data_dir, "scripts/fox.bin", &g_tables[MSL_CHAR_FOX]) != 0) {
    return -1;
  }
  if (script_table_load(data_dir, "scripts/falco.bin", &g_tables[MSL_CHAR_FALCO]) != 0) {
    script_table_free(&g_tables[MSL_CHAR_FOX]);
    return -1;
  }
  g_loaded = 1;
  return 0;
}

static const MslScriptEntry* script_table_find_entry(const MslScriptTable* table, uint16_t msid) {
  if (table == NULL || table->entries == NULL) {
    return NULL;
  }
  size_t lo = 0;
  size_t hi = table->entry_count;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2u;
    const uint16_t got = table->entries[mid].msid;
    if (got == msid) {
      return &table->entries[mid];
    }
    if (got < msid) {
      lo = mid + 1u;
    } else {
      hi = mid;
    }
  }
  return NULL;
}

MslScriptEventRange script_events_range(uint8_t char_id, uint16_t msid) {
  const MslScriptTable* table = &g_tables[char_id];
  const MslScriptEntry* entry = script_table_find_entry(table, msid);
  if (entry == NULL || entry->first_event > table->event_count ||
      entry->event_count > table->event_count - entry->first_event) {
    return (MslScriptEventRange){0};
  }
  return (MslScriptEventRange){.events = table->events + entry->first_event,
                               .count = entry->event_count};
}

const MslScriptEvent* script_events_first(uint8_t char_id, uint16_t msid, MslScriptEventKind kind) {
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    if (range.events[i].kind_id == (uint16_t)kind) {
      return &range.events[i];
    }
  }
  return NULL;
}

uint8_t script_events_window_contains(MslScriptFrameWindow win, float frame) {
  return (win.loaded && frame >= (float)win.start_af && frame < (float)win.end_af) ? 1u : 0u;
}

uint8_t script_events_frame_crossed(uint16_t frame, float prev_frame, float cur_frame) {
  const float on = (float)frame;
  return (prev_frame < on && cur_frame >= on) ? 1u : 0u;
}

uint8_t script_events_window_crossed(MslScriptFrameWindow win, float prev_frame, float cur_frame) {
  return (win.loaded && script_events_frame_crossed((uint16_t)win.start_af, prev_frame, cur_frame))
             ? 1u
             : 0u;
}

uint8_t script_events_cmd_var_window(uint8_t char_id, uint16_t msid, uint8_t idx, uint8_t open_end,
                                     MslScriptFrameWindow* out) {
  if (out == NULL) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){0};
  int on_frame = -1;
  int off_frame = -1;
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id != (uint16_t)MSL_SCRIPT_EVENT_SET_CMD_VAR || ev->payload.cmd_var.idx != idx) {
      continue;
    }
    if (ev->payload.cmd_var.value != 0u && on_frame < 0) {
      on_frame = (int)ev->frame;
    } else if (ev->payload.cmd_var.value == 0u && on_frame >= 0 && off_frame < 0) {
      off_frame = (int)ev->frame;
    }
  }
  if (on_frame < 0) {
    return 0u;
  }
  if (off_frame < 0) {
    if (!open_end) {
      return 0u;
    }
    off_frame = INT16_MAX;
  }
  if (off_frame < on_frame) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){
      .start_af = (int16_t)on_frame, .end_af = (int16_t)off_frame, .loaded = 1u};
  return 1u;
}

uint8_t script_events_cmd_var_value_window(uint8_t char_id, uint16_t msid, uint8_t idx,
                                           uint8_t value, uint8_t open_end,
                                           MslScriptFrameWindow* out) {
  if (out == NULL) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){0};
  int on_frame = -1;
  int off_frame = -1;
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id != (uint16_t)MSL_SCRIPT_EVENT_SET_CMD_VAR || ev->payload.cmd_var.idx != idx) {
      continue;
    }
    if (ev->payload.cmd_var.value == value && on_frame < 0) {
      on_frame = (int)ev->frame;
    } else if (ev->payload.cmd_var.value != value && on_frame >= 0 && off_frame < 0) {
      off_frame = (int)ev->frame;
    }
  }
  if (on_frame < 0) {
    return 0u;
  }
  if (off_frame < 0) {
    if (!open_end) {
      return 0u;
    }
    off_frame = INT16_MAX;
  }
  if (off_frame < on_frame) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){
      .start_af = (int16_t)on_frame, .end_af = (int16_t)off_frame, .loaded = 1u};
  return 1u;
}

uint8_t script_events_allow_interrupt_window(uint8_t char_id, uint16_t msid,
                                             MslScriptFrameWindow* out) {
  if (out == NULL) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){0};
  const MslScriptEvent* ev = script_events_first(char_id, msid, MSL_SCRIPT_EVENT_ALLOW_INTERRUPT);
  if (ev == NULL) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){
      .start_af = (int16_t)ev->frame, .end_af = (int16_t)INT16_MAX, .loaded = 1u};
  return 1u;
}

uint8_t script_events_throw_flags_window(uint8_t char_id, uint16_t msid, uint8_t hit_idx,
                                         uint8_t use_hit_idx, MslScriptFrameWindow* out) {
  if (out == NULL) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){0};
  int on_frame = -1;
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id != (uint16_t)MSL_SCRIPT_EVENT_SET_THROW_FLAGS) {
      continue;
    }
    if (!use_hit_idx || ev->payload.throw_flags.hit_idx == hit_idx) {
      if (on_frame < 0 || (int)ev->frame < on_frame) {
        on_frame = (int)ev->frame;
      }
    }
  }
  if (on_frame < 0) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){
      .start_af = (int16_t)on_frame, .end_af = (int16_t)INT16_MAX, .loaded = 1u};
  return 1u;
}

uint8_t script_events_throw_flags_pulses(uint8_t char_id, uint16_t msid, uint8_t hit_idx,
                                         uint16_t* out_frames, uint8_t max_out,
                                         uint8_t* out_count) {
  if (out_frames == NULL || out_count == NULL || max_out == 0u) {
    return 0u;
  }
  *out_count = 0u;
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count && *out_count < max_out; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_SET_THROW_FLAGS &&
        ev->payload.throw_flags.hit_idx == hit_idx) {
      uint8_t dup = 0u;
      for (uint8_t j = 0; j < *out_count; j++) {
        if (out_frames[j] == ev->frame) {
          dup = 1u;
          break;
        }
      }
      if (!dup) {
        out_frames[*out_count] = ev->frame;
        *out_count = (uint8_t)(*out_count + 1u);
      }
    }
  }
  return *out_count != 0u ? 1u : 0u;
}

uint8_t script_events_first_create_hitbox_phase(uint8_t char_id, uint16_t msid,
                                                MslScriptFrameWindow* out) {
  if (out == NULL) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){0};
  int first_create = -1;
  int first_clear = -1;
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CREATE_HITBOX && first_create < 0) {
      first_create = (int)ev->frame;
    } else if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CLEAR_HITBOXES && first_create >= 0) {
      first_clear = (int)ev->frame;
      break;
    }
  }
  if (first_create < 0) {
    return 0u;
  }
  *out =
      (MslScriptFrameWindow){.start_af = (int16_t)first_create,
                             .end_af = (int16_t)((first_clear >= 0) ? first_clear + 1 : INT16_MAX),
                             .loaded = 1u};
  return 1u;
}

uint8_t script_events_hitbox_lifetime(uint8_t char_id, uint16_t msid, MslScriptFrameWindow* out) {
  if (out == NULL) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){0};
  int first_create = -1;
  int last_clear = -1;
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CREATE_HITBOX && first_create < 0) {
      first_create = (int)ev->frame;
    } else if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CLEAR_HITBOXES && first_create >= 0) {
      last_clear = (int)ev->frame;
    }
  }
  if (first_create < 0) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){.start_af = (int16_t)first_create,
                                .end_af = (int16_t)((last_clear >= 0) ? last_clear + 1 : INT16_MAX),
                                .loaded = 1u};
  return 1u;
}

uint8_t script_events_second_create_hitbox_phase(uint8_t char_id, uint16_t msid,
                                                 MslScriptFrameWindow* out) {
  if (out == NULL) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){0};
  int first_frame = -1;
  int second_frame = -1;
  int clear_frame = -1;
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CREATE_HITBOX) {
      if (first_frame < 0) {
        first_frame = (int)ev->frame;
      } else if (second_frame < 0 && (int)ev->frame != first_frame) {
        second_frame = (int)ev->frame;
      }
    } else if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CLEAR_HITBOXES && second_frame >= 0 &&
               (int)ev->frame >= second_frame) {
      clear_frame = (int)ev->frame;
      break;
    }
  }
  if (second_frame < 0) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){.start_af = (int16_t)second_frame,
                                .end_af = (int16_t)((clear_frame >= 0) ? clear_frame : INT16_MAX),
                                .loaded = 1u};
  return 1u;
}

uint8_t script_events_last_create_hitbox_phase(uint8_t char_id, uint16_t msid,
                                               MslScriptFrameWindow* out) {
  if (out == NULL) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){0};
  int last_create = -1;
  int last_seen_create = -1;
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CREATE_HITBOX &&
        (int)ev->frame != last_seen_create) {
      last_seen_create = (int)ev->frame;
      last_create = (int)ev->frame;
    }
  }
  if (last_create < 0) {
    return 0u;
  }
  int clear_frame = INT16_MAX;
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CLEAR_HITBOXES && (int)ev->frame >= last_create) {
      clear_frame = (int)ev->frame;
      break;
    }
  }
  *out = (MslScriptFrameWindow){
      .start_af = (int16_t)last_create, .end_af = (int16_t)clear_frame, .loaded = 1u};
  return 1u;
}

uint8_t script_events_post_clear_create_hitbox_phase(uint8_t char_id, uint16_t msid,
                                                     MslScriptFrameWindow* out) {
  if (out == NULL) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){0};
  int first_create_after_clear = -1;
  int last_clear_after_first_create = -1;
  int seen_create = 0;
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CREATE_HITBOX) {
      seen_create = 1;
      if (last_clear_after_first_create >= 0 && first_create_after_clear < 0) {
        first_create_after_clear = (int)ev->frame;
      }
    } else if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CLEAR_HITBOXES && seen_create) {
      last_clear_after_first_create = (int)ev->frame;
    }
  }
  if (first_create_after_clear < 0) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){
      .start_af = (int16_t)first_create_after_clear,
      .end_af = (int16_t)((last_clear_after_first_create >= first_create_after_clear)
                              ? last_clear_after_first_create + 1
                              : INT16_MAX),
      .loaded = 1u};
  return 1u;
}

uint8_t script_events_catchattack_grabbed_hit_window(uint8_t char_id, uint16_t msid,
                                                     MslScriptFrameWindow* out) {
  if (out == NULL) {
    return 0u;
  }
  *out = (MslScriptFrameWindow){0};
  int on_frame = -1;
  int off_frame = -1;
  const MslScriptEventRange range = script_events_range(char_id, msid);
  for (uint32_t i = 0; i < range.count; i++) {
    const MslScriptEvent* ev = &range.events[i];
    if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CREATE_HITBOX) {
      if ((ev->payload.create_hitbox.flags & MSL_SCRIPT_CREATE_HITBOX_FLAG_ONLY_HIT_GRABBED) !=
          0u) {
        if (on_frame < 0 || (int)ev->frame < on_frame) {
          on_frame = (int)ev->frame;
        }
      }
    } else if (ev->kind_id == (uint16_t)MSL_SCRIPT_EVENT_CLEAR_HITBOXES && on_frame >= 0 &&
               (int)ev->frame >= on_frame) {
      if (off_frame < 0 || (int)ev->frame < off_frame) {
        off_frame = (int)ev->frame;
      }
    }
  }
  if (on_frame < 0) {
    return 0u;
  }
  if (off_frame < 0) {
    off_frame = on_frame + 1;
  }
  *out = (MslScriptFrameWindow){
      .start_af = (int16_t)on_frame, .end_af = (int16_t)off_frame, .loaded = 1u};
  return 1u;
}
