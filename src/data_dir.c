#include "data_dir.h"

#include <stdlib.h>
#include <string.h>

enum { MSL_DATA_DIR_MAX = 1024 };

static char g_data_dir_override[MSL_DATA_DIR_MAX];
static int g_data_dir_override_set = 0;

const char* msl_data_dir(void) {
  if (g_data_dir_override_set) {
    return g_data_dir_override;
  }
  const char* env = getenv("MSL_DATA_DIR");
  if (env != NULL && env[0] != '\0') {
    return env;
  }
  return "data";
}

void msl_clear_data_dir(void) {
  g_data_dir_override_set = 0;
  g_data_dir_override[0] = '\0';
}

int msl_set_data_dir(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return 1;
  }
  const size_t n = strlen(path);
  if (n >= (size_t)MSL_DATA_DIR_MAX) {
    return 1;
  }
  memcpy(g_data_dir_override, path, n + 1u);
  g_data_dir_override_set = 1;
  return 0;
}
