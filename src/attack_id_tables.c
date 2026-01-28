#include "attack_id_tables.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alloc.h"
#include "staling.h"

// Character id mapping follows Slippi post-frame `character` (GALE01):
// - Fox   = 1
// - Falco = 22
enum { MSL_CHAR_FOX = 1, MSL_CHAR_FALCO = 22 };

enum {
  TABLE_MAGIC_LEN = 8,
  TABLE_HDR_BYTES = 8 + 4 + 2 + 2 + 4 + 4,
};

static const uint8_t k_magic[TABLE_MAGIC_LEN] = {'M', 'S', 'L', 'A', 'C', 'I', 'D', '1'};
static const uint32_t k_format_version = 1;

typedef struct {
  uint8_t* buf;
  size_t sz;
  uint16_t* move_id_by_action;
  uint16_t action_count;
  uint8_t have;
} MslActionMoveIdTable;

static MslActionMoveIdTable g_table_by_char[256];
// 0 = not attempted, 1 = loaded ok, -1 = attempted and failed (avoid spam).
static int g_load_state = 0;

static void table_free(MslActionMoveIdTable* t) {
  if (t == NULL) {
    return;
  }
  if (t->buf != NULL) {
    alloc_free(t->buf);
  }
  memset(t, 0, sizeof(*t));
}

static uint16_t read_u16_le(const uint8_t* p) {
  uint16_t v = 0;
  memcpy(&v, p, sizeof(v));
  return v;
}

static uint32_t read_u32_le(const uint8_t* p) {
  uint32_t v = 0;
  memcpy(&v, p, sizeof(v));
  return v;
}

static const char* data_dir_or_default(void) {
  const char* data_dir = getenv("MSL_DATA_DIR");
  if (data_dir == NULL || data_dir[0] == '\0') {
    data_dir = "data";
  }
  return data_dir;
}

static void print_generate_hint(const char* data_dir) {
  const char* dd = (data_dir != NULL && data_dir[0] != '\0') ? data_dir : "data";
  fprintf(stderr,
          "msl: generate tables with:\n"
          "  uv run python -m tools.extraction.extract_attack_id_move_id "
          "--melee_decomp refs/melee --out_dir %s/attack_id/move_id --chars fox,falco\n",
          dd);
}

// Returns:
// - 0 on success
// - -2 on open failure (out_errno set)
// - -3 on other I/O failure (out_errno best-effort)
static int load_file_buf(const char* path, uint8_t** out_buf, size_t* out_sz, int* out_errno) {
  if (path == NULL || out_buf == NULL || out_sz == NULL) {
    return -1;
  }
  *out_buf = NULL;
  *out_sz = 0;

  FILE* f = fopen(path, "rb");
  if (f == NULL) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    return -2;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    fclose(f);
    return -3;
  }
  const long sz_long = ftell(f);
  if (sz_long <= 0) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    fclose(f);
    return -3;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    fclose(f);
    return -3;
  }

  const size_t sz = (size_t)sz_long;
  uint8_t* buf = (uint8_t*)alloc_malloc(sz);
  if (buf == NULL) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    fclose(f);
    return -3;
  }
  const size_t got = fread(buf, 1, sz, f);
  fclose(f);
  if (got != sz) {
    if (out_errno != NULL) {
      *out_errno = errno;
    }
    alloc_free(buf);
    return -3;
  }

  *out_buf = buf;
  *out_sz = sz;
  return 0;
}

static int load_table_for_char_into(uint8_t char_id, const char* rel_name, MslActionMoveIdTable* out) {
  if (out == NULL) {
    return -1;
  }
  memset(out, 0, sizeof(*out));

  const char* data_dir = data_dir_or_default();
  char path[512];
  const int n = snprintf(path, sizeof(path), "%s/attack_id/move_id/%s.bin", data_dir, rel_name);
  if (n <= 0 || (size_t)n >= sizeof(path)) {
    fprintf(stderr, "msl: attack_id move_id table path too long (MSL_DATA_DIR=%s)\n", data_dir);
    print_generate_hint(data_dir);
    return -1;
  }

  uint8_t* buf = NULL;
  size_t sz = 0;
  int open_errno = 0;
  const int load_rc = load_file_buf(path, &buf, &sz, &open_errno);
  if (load_rc != 0) {
    if (load_rc == -2) {
      fprintf(stderr, "msl: could not open attack_id move_id table for char_id=%u: %s (%s)\n",
              (unsigned)char_id, path, strerror(open_errno));
    } else {
      fprintf(stderr, "msl: failed to read attack_id move_id table for char_id=%u: %s (%s)\n",
              (unsigned)char_id, path, strerror(open_errno));
    }
    fprintf(stderr, "msl: set MSL_DATA_DIR to point at the extracted data root if needed\n");
    print_generate_hint(data_dir);
    return -1;
  }
  if (sz < TABLE_HDR_BYTES) {
    fprintf(stderr, "msl: attack_id move_id table header too small for char_id=%u: %s (sz=%zu)\n",
            (unsigned)char_id, path, sz);
    print_generate_hint(data_dir);
    alloc_free(buf);
    return -1;
  }
  if (memcmp(buf, k_magic, TABLE_MAGIC_LEN) != 0) {
    fprintf(stderr, "msl: attack_id move_id table bad magic for char_id=%u: %s\n", (unsigned)char_id,
            path);
    print_generate_hint(data_dir);
    alloc_free(buf);
    return -1;
  }

  const uint32_t ver = read_u32_le(buf + 8);
  if (ver != k_format_version) {
    fprintf(stderr,
            "msl: attack_id move_id table bad version for char_id=%u: %s (got=%u expected=%u)\n",
            (unsigned)char_id, path, (unsigned)ver, (unsigned)k_format_version);
    print_generate_hint(data_dir);
    alloc_free(buf);
    return -1;
  }
  const uint16_t action_count = read_u16_le(buf + 12);
  const uint32_t toc_off = read_u32_le(buf + 16);
  const uint32_t file_bytes = read_u32_le(buf + 20);
  if (file_bytes != (uint32_t)sz) {
    fprintf(stderr,
            "msl: attack_id move_id table file_bytes mismatch for char_id=%u: %s (hdr=%u actual=%zu)\n",
            (unsigned)char_id, path, (unsigned)file_bytes, sz);
    print_generate_hint(data_dir);
    alloc_free(buf);
    return -1;
  }
  if (toc_off < TABLE_HDR_BYTES) {
    fprintf(stderr, "msl: attack_id move_id table toc_off too small for char_id=%u: %s (toc_off=%u)\n",
            (unsigned)char_id, path, (unsigned)toc_off);
    print_generate_hint(data_dir);
    alloc_free(buf);
    return -1;
  }
  if (toc_off + (uint32_t)action_count * 2u > (uint32_t)sz) {
    fprintf(stderr,
            "msl: attack_id move_id table toc out of bounds for char_id=%u: %s "
            "(toc_off=%u count=%u sz=%zu)\n",
            (unsigned)char_id, path, (unsigned)toc_off, (unsigned)action_count, sz);
    print_generate_hint(data_dir);
    alloc_free(buf);
    return -1;
  }

  *out = (MslActionMoveIdTable){
      .buf = buf,
      .sz = sz,
      .move_id_by_action = (uint16_t*)(void*)(buf + toc_off),
      .action_count = action_count,
      .have = 1,
  };
  return 0;
}

int attack_id_tables_init(void) {
  if (g_load_state == 1) {
    return 0;
  }
  if (g_load_state < 0) {
    return -1;
  }

  // Atomic initialization: fully load all supported tables first, then publish.
  MslActionMoveIdTable fox = {0};
  MslActionMoveIdTable falco = {0};

  if (load_table_for_char_into((uint8_t)MSL_CHAR_FOX, "fox", &fox) != 0) {
    table_free(&fox);
    table_free(&falco);
    g_load_state = -1;
    return -1;
  }
  if (load_table_for_char_into((uint8_t)MSL_CHAR_FALCO, "falco", &falco) != 0) {
    table_free(&fox);
    table_free(&falco);
    g_load_state = -1;
    return -1;
  }

  memset(g_table_by_char, 0, sizeof(g_table_by_char));
  g_table_by_char[(uint8_t)MSL_CHAR_FOX] = fox;
  g_table_by_char[(uint8_t)MSL_CHAR_FALCO] = falco;
  g_load_state = 1;
  return 0;
}

uint16_t attack_id_move_id_from_action(uint8_t char_id, uint16_t action_id) {
  const MslActionMoveIdTable* t = &g_table_by_char[char_id];
  if (!t->have || t->move_id_by_action == NULL || t->action_count == 0) {
    return (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  }
  if (action_id >= t->action_count) {
    return (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  }
  const uint16_t mv = t->move_id_by_action[action_id];
  if (mv == 0xFFFFu) {
    return (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  }
  return mv;
}
