from __future__ import annotations

from pathlib import Path

from setuptools import Extension, setup


def get_numpy_include():
    import numpy

    return numpy.get_include()


ROOT = Path(__file__).resolve().parent.parent

ext = Extension(
    name="msl_binding",
    sources=[
        str((ROOT / "python" / "msl_binding.c").resolve()),
        str((ROOT / "src" / "api.c").resolve()),
        str((ROOT / "src" / "alloc.c").resolve()),
        str((ROOT / "src" / "config.c").resolve()),
        str((ROOT / "src" / "state.c").resolve()),
        str((ROOT / "src" / "step.c").resolve()),
        str((ROOT / "src" / "input.c").resolve()),
        str((ROOT / "src" / "action.c").resolve()),
        str((ROOT / "src" / "physics.c").resolve()),
        str((ROOT / "src" / "stage_collision.c").resolve()),
        str((ROOT / "src" / "hurtboxes.c").resolve()),
        str((ROOT / "src" / "combat.c").resolve()),
        str((ROOT / "src" / "items.c").resolve()),
    ],
    include_dirs=[get_numpy_include(), str((ROOT / "src").resolve())],
    extra_compile_args=["-O3", "-Wall", "-Wextra", "-std=c11"],
)

setup(
    name="melee-sim-light",
    version="0.0.0",
    py_modules=[],
    ext_modules=[ext],
)
