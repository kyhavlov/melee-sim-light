from __future__ import annotations

from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


def get_numpy_include():
    import numpy

    return numpy.get_include()


def _compiler_supports_flag(compiler, flag: str) -> bool:
    import tempfile

    if getattr(compiler, "compiler_type", "") == "msvc":
        return False
    try:
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            src = p / "flagcheck.c"
            src.write_text("int main(void){return 0;}\n")
            compiler.compile([str(src)], output_dir=str(p), extra_postargs=[flag])
        return True
    except Exception:
        return False


class _BuildExt(build_ext):
    def build_extensions(self) -> None:
        # Prevent implicit FMA contraction (cross-machine bitwise risk).
        # GCC defaults `-ffp-contract=fast`; Clang generally supports this flag too.
        flag = "-ffp-contract=off"
        if _compiler_supports_flag(self.compiler, flag):
            for ext in self.extensions:
                ext.extra_compile_args = list(ext.extra_compile_args or [])
                if flag not in ext.extra_compile_args:
                    ext.extra_compile_args.append(flag)
        super().build_extensions()


ROOT = Path(__file__).resolve().parent.parent

ext = Extension(
    name="msl_binding",
    sources=[
        str((ROOT / "python" / "msl_binding.c").resolve()),
        str((ROOT / "src" / "api.c").resolve()),
        str((ROOT / "src" / "alloc.c").resolve()),
        str((ROOT / "src" / "config.c").resolve()),
        str((ROOT / "src" / "common_params.c").resolve()),
        str((ROOT / "src" / "char_params.c").resolve()),
        str((ROOT / "src" / "anim_table.c").resolve()),
        str((ROOT / "src" / "anim_pose.c").resolve()),
        str((ROOT / "src" / "anim_timebase.c").resolve()),
        str((ROOT / "src" / "ecb_tables.c").resolve()),
        str((ROOT / "src" / "shield_tilt_table.c").resolve()),
        str((ROOT / "src" / "laser_params.c").resolve()),
        str((ROOT / "src" / "state.c").resolve()),
        str((ROOT / "src" / "step.c").resolve()),
        str((ROOT / "src" / "input.c").resolve()),
        str((ROOT / "src" / "ucf.c").resolve()),
        str((ROOT / "src" / "action.c").resolve()),
        str((ROOT / "src" / "knockdown.c").resolve()),
        str((ROOT / "src" / "blaster.c").resolve()),
        str((ROOT / "src" / "ledge.c").resolve()),
        str((ROOT / "src" / "timers.c").resolve()),
        str((ROOT / "src" / "decomp" / "lb" / "lb_00ce.c").resolve()),
        str((ROOT / "src" / "physics.c").resolve()),
        str((ROOT / "src" / "stage_collision.c").resolve()),
        str((ROOT / "src" / "mpcoll_ground.c").resolve()),
        str((ROOT / "src" / "match_flow.c").resolve()),
        str((ROOT / "src" / "locomotion.c").resolve()),
        str((ROOT / "src" / "move_tables.c").resolve()),
        str((ROOT / "src" / "hurtcaps_tables.c").resolve()),
        str((ROOT / "src" / "hurtbox_modes_tables.c").resolve()),
        str((ROOT / "src" / "hurtboxes.c").resolve()),
        str((ROOT / "src" / "hit_status_tables.c").resolve()),
        str((ROOT / "src" / "hitboxes_tables.c").resolve()),
        str((ROOT / "src" / "hitboxes.c").resolve()),
        str((ROOT / "src" / "shields.c").resolve()),
        str((ROOT / "src" / "combat.c").resolve()),
        str((ROOT / "src" / "items.c").resolve()),
    ],
    include_dirs=[get_numpy_include(), str((ROOT / "src").resolve())],
    libraries=["m"],
    extra_compile_args=["-O3", "-Wall", "-Wextra", "-std=c11"],
)

setup(
    name="melee-sim-light",
    version="0.0.0",
    py_modules=[],
    ext_modules=[ext],
    cmdclass={"build_ext": _BuildExt},
)
