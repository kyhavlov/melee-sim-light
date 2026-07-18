from __future__ import annotations

import ctypes
import os
from functools import lru_cache
from pathlib import Path

import numpy as np


class NativeError(RuntimeError):
    pass


_RESULTS = {
    1: "invalid argument",
    2: "out of memory",
    3: "invalid simulator state",
    4: "incompatible game data or savestate",
}


def _library_path() -> Path:
    override = os.environ.get("MSL_CORE_LIBRARY")
    if override:
        return Path(override).expanduser().resolve()
    bundled = Path(__file__).with_name("libmelee_core.so")
    if bundled.is_file():
        return bundled
    checkout = Path(__file__).resolve().parents[1]
    built = checkout / "build/melee_core/python/libmelee_core.so"
    if built.is_file():
        return built
    raise FileNotFoundError(
        "canonical melee core library is not built; run `make python-library` "
        "or set MSL_CORE_LIBRARY"
    )


@lru_cache(maxsize=1)
def library() -> ctypes.CDLL:
    lib = ctypes.CDLL(str(_library_path()))
    pointer = ctypes.c_void_p
    size = ctypes.c_size_t
    u32 = ctypes.c_uint32

    lib.msl_core_game_data_create.argtypes = [ctypes.c_char_p, ctypes.POINTER(pointer)]
    lib.msl_core_game_data_create.restype = ctypes.c_int
    lib.msl_core_game_data_destroy.argtypes = [pointer]
    lib.msl_core_batch_create.argtypes = [pointer, u32, ctypes.POINTER(pointer)]
    lib.msl_core_batch_create.restype = ctypes.c_int
    lib.msl_core_batch_destroy.argtypes = [pointer]
    lib.msl_core_batch_match_count.argtypes = [pointer]
    lib.msl_core_batch_match_count.restype = u32

    for name in (
        "msl_core_batch_reset_matches",
        "msl_core_batch_step_matches",
        "msl_core_batch_write_state",
        "msl_core_batch_write_observation",
        "msl_core_batch_write_terminal",
    ):
        getattr(lib, name).restype = ctypes.c_int
    lib.msl_core_batch_reset_matches.argtypes = [pointer, pointer, size, pointer, size]
    lib.msl_core_batch_step_matches.argtypes = [pointer, pointer, size, pointer, size]
    lib.msl_core_batch_write_state.argtypes = [pointer, pointer, size, pointer, size]
    lib.msl_core_batch_write_observation.argtypes = [
        pointer,
        pointer,
        size,
        pointer,
        size,
        pointer,
        size,
    ]
    lib.msl_core_batch_write_terminal.argtypes = [
        pointer,
        pointer,
        size,
        ctypes.c_int32,
        pointer,
        size,
    ]
    lib.msl_core_batch_copy_matches.argtypes = [pointer, pointer, pointer, pointer, u32]
    lib.msl_core_batch_copy_matches.restype = ctypes.c_int
    lib.msl_core_batch_match_save_size.argtypes = [pointer, u32, ctypes.POINTER(size)]
    lib.msl_core_batch_match_save_size.restype = ctypes.c_int
    lib.msl_core_batch_save_match.argtypes = [
        pointer,
        u32,
        pointer,
        size,
        ctypes.POINTER(size),
    ]
    lib.msl_core_batch_save_match.restype = ctypes.c_int
    lib.msl_core_batch_restore_match.argtypes = [pointer, u32, pointer, size]
    lib.msl_core_batch_restore_match.restype = ctypes.c_int
    lib.msl_python_controller_inputs.argtypes = [pointer, size, pointer, size, u32]
    lib.msl_python_controller_inputs.restype = ctypes.c_int
    return lib


def pointer(array: np.ndarray | None) -> ctypes.c_void_p:
    if array is None:
        return ctypes.c_void_p()
    if not array.flags.c_contiguous:
        raise ValueError("native API arrays must be C-contiguous")
    return ctypes.c_void_p(int(array.ctypes.data))


def check(result: int, operation: str) -> None:
    if result:
        raise NativeError(f"{operation}: {_RESULTS.get(result, f'error {result}')}")
