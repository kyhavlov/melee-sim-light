#pragma once

// Process-wide data root resolution for the table loaders.
//
// Resolution order: explicit override (msl_set_data_dir, set by host bindings before init)
// -> MSL_DATA_DIR environment variable -> "data" (repo-relative default).
//
// The override exists so embedding APIs (melee_sim.EnvBatch) can choose a data root
// per-process WITHOUT mutating the host's environment: os.environ writes from inside a
// library constructor leak into unrelated code (the order-dependence class found in
// test_melee_sim_api). The loaders run once per process (g_loaded latches), so this is
// process-global by the same necessity the env var was.
const char* msl_data_dir(void);

// Returns 0 on success, nonzero if path is NULL/empty or too long.
int msl_set_data_dir(const char* path);

// Drop the override so resolution falls back to MSL_DATA_DIR/"data". Synthetic-data
// tests that swap the env var in-process must clear a prior override or it silently
// wins (msl_debug_reset_pose_and_hitboxes_tables clears it for the table-swap path).
void msl_clear_data_dir(void);
