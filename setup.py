from __future__ import annotations

import shutil
from pathlib import Path

from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext
from setuptools.command.build_py import build_py


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


class _BuildPy(build_py):
    def run(self) -> None:
        if self.build_lib:
            shutil.rmtree(self.build_lib, ignore_errors=True)
        super().run()


ROOT = Path(__file__).resolve().parent
DEPENDS = sorted(
    str(p.relative_to(ROOT))
    for base in (ROOT / "src", ROOT / "python")
    for p in base.rglob("*.h")
)

ext = Extension(
    name="melee_sim._native",
    sources=[
        "python/msl_binding.c",
        "python/msl_preprocess_native.c",
        "python/msl_taxonomy_native.c",
        "src/api.c",
        "src/alloc.c",
        "src/config.c",
        "src/common_params.c",
        "src/char_params.c",
        "src/special_msids.c",
        "src/anim_table.c",
        "src/anim_pose.c",
        "src/anim_timebase.c",
        "src/ecb_tables.c",
        "src/shield_tilt_table.c",
        "src/laser_params.c",
        "src/item_common_params.c",
        "src/item_article_params.c",
        "src/stage_item_params.c",
        "src/state.c",
        "src/step.c",
        "src/fighter_callbacks.c",
        "src/input.c",
        "src/ucf.c",
        "src/action.c",
        "src/knockdown.c",
        "src/blaster.c",
        "src/shine.c",
        "src/ledge.c",
        "src/grab_attachment.c",
        "src/grab_flow.c",
        "src/throw_flow.c",
        "src/timers.c",
        "src/decomp/lb/lb_00ce.c",
        "src/physics.c",
        "src/stage_collision.c",
        "src/mpcoll_ground.c",
        "src/mpcoll_wall_ceil.c",
        "src/mpcoll_env.c",
        "src/match_flow.c",
        "src/locomotion.c",
        "src/move_tables.c",
        "src/script_events.c",
        "src/attack_id_tables.c",
        "src/motion_state_owners.c",
        "src/attack_identity.c",
        "src/instance_id.c",
        "src/hurtcaps_tables.c",
        "src/hurtboxes.c",
        "src/hitboxes_tables.c",
        "src/hitboxes.c",
        "src/hitlist.c",
        "src/staling_tables.c",
        "src/staling.c",
        "src/shields.c",
        "src/reflector_bubbles.c",
        "src/state_flags.c",
        "src/combat.c",
        "src/items.c",
    ],
    include_dirs=[get_numpy_include(), "src"],
    depends=DEPENDS,
    libraries=["m"],
    extra_compile_args=["-O3", "-Wall", "-Wextra", "-std=c11"],
)

setup(
    name="melee-sim-light",
    version="0.0.0",
    packages=["melee_sim", "tools", "tools.extraction"],
    py_modules=["msl_binding"],
    ext_modules=[ext],
    cmdclass={"build_ext": _BuildExt, "build_py": _BuildPy},
)
