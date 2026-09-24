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

    lib.msl_game_data_acquire.argtypes = [ctypes.c_char_p]
    lib.msl_game_data_acquire.restype = ctypes.c_int
    lib.msl_game_data_release.argtypes = []
    lib.msl_game_data_release.restype = None
    lib.msl_game_data_references.argtypes = []
    lib.msl_game_data_references.restype = u32
    lib.msl_batch_create.argtypes = [ctypes.c_char_p, u32, ctypes.POINTER(pointer)]
    lib.msl_batch_create.restype = ctypes.c_int
    lib.msl_batch_destroy.argtypes = [pointer]
    lib.msl_batch_size.argtypes = [pointer]
    lib.msl_batch_size.restype = u32
    lib.msl_batch_reset.argtypes = [pointer, pointer, pointer, pointer]
    lib.msl_batch_reset.restype = ctypes.c_int
    lib.msl_batch_step.argtypes = [pointer, pointer, pointer, pointer]
    lib.msl_batch_step.restype = ctypes.c_int
    lib.msl_batch_step_masked.argtypes = [pointer, pointer, pointer, pointer, pointer]
    lib.msl_batch_step_masked.restype = ctypes.c_int
    lib.msl_batch_observe.argtypes = [pointer, pointer, pointer]
    lib.msl_batch_observe.restype = ctypes.c_int
    lib.msl_batch_copy.argtypes = [pointer, pointer, pointer, pointer, u32]
    lib.msl_batch_copy.restype = ctypes.c_int
    lib.msl_batch_save_size.argtypes = [pointer, u32, ctypes.POINTER(size)]
    lib.msl_batch_save_size.restype = ctypes.c_int
    lib.msl_batch_save.argtypes = [
        pointer,
        u32,
        pointer,
        size,
        ctypes.POINTER(size),
    ]
    lib.msl_batch_save.restype = ctypes.c_int
    lib.msl_batch_restore.argtypes = [pointer, u32, pointer, size]
    lib.msl_batch_restore.restype = ctypes.c_int
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
