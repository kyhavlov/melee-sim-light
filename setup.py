from __future__ import annotations

import os
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


def _env_flag(name: str, default: bool) -> bool:
    value = os.environ.get(name)
    if value is None:
        return default
    return value.strip().lower() not in {"0", "false", "no", "off"}


class _BuildExt(build_ext):
    def build_extensions(self) -> None:
        # Prevent implicit FMA contraction (cross-machine bitwise risk).
        # GCC defaults `-ffp-contract=fast`; Clang generally supports this flag too.
        self._append_supported_compile_flag("-ffp-contract=off")

        # Keep package/source-install artifacts portable by default. Perf runs can opt in with
        # MSL_NATIVE_OPT=1, or through the Makefile's build-native / bench-sim-native targets.
        if _env_flag("MSL_NATIVE_OPT", False):
            self._append_supported_compile_flag("-march=native")

        # LTO is also opt-in: it improves throughput on local profiles, but increases build cost
        # and can produce less portable artifacts.
        if _env_flag("MSL_LTO", False) and self._append_supported_compile_flag("-flto"):
            for ext in self.extensions:
                ext.extra_link_args = list(ext.extra_link_args or [])
                if "-flto" not in ext.extra_link_args:
                    ext.extra_link_args.append("-flto")

        super().build_extensions()

    def _append_supported_compile_flag(self, flag: str) -> bool:
        if _compiler_supports_flag(self.compiler, flag):
            for ext in self.extensions:
                ext.extra_compile_args = list(ext.extra_compile_args or [])
                if flag not in ext.extra_compile_args:
                    ext.extra_compile_args.append(flag)
            return True
        return False


class _BuildPy(build_py):
    def run(self) -> None:
        if self.build_lib:
            shutil.rmtree(self.build_lib, ignore_errors=True)
        super().run()


ROOT = Path(__file__).resolve().parent
DEPENDS = sorted(
    str(p.relative_to(ROOT))
    for base in (ROOT / "src", ROOT / "bindings")
    for p in base.rglob("*.h")
)

ext = Extension(
    name="melee_sim._native",
    sources=[
        "bindings/msl_binding.c",
        "bindings/msl_binding_data.c",
        "bindings/msl_binding_debug.c",
        "bindings/msl_binding_disruptive.c",
        "bindings/msl_binding_runtime.c",
        "bindings/msl_binding_validation.c",
        "bindings/msl_preprocess_anim.c",
        "bindings/msl_preprocess_common.c",
        "bindings/msl_preprocess_items.c",
        "bindings/msl_taxonomy_native.c",
        "bindings/msl_validation_buffers.c",
        "bindings/msl_validation_combat.c",
        "bindings/msl_validation_damage_history.c",
        "bindings/msl_validation_input.c",
        "bindings/msl_validation_movement_history.c",
        "bindings/msl_validation_specials.c",
        "bindings/msl_validation_items.c",
        "bindings/msl_validation_stage.c",
        "src/api.c",
        "src/alloc.c",
        "src/config.c",
        "src/data_dir.c",
        "src/common_params.c",
        "src/common_specials.c",
        "src/char_params.c",
        "src/special_msids.c",
        "src/anim_table.c",
        "src/anim_pose.c",
        "src/anim_timebase.c",
        "src/ecb_tables.c",
        "src/ecb_pose.c",
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
        "src/spacie_specials.c",
        "src/ledge.c",
        "src/grab_attachment.c",
        "src/grab_flow.c",
        "src/throw_flow.c",
        "src/timers.c",
        "src/decomp/lb/lb_00ce.c",
        "src/physics.c",
        "src/stage_collision.c",
        "src/mpcoll_ground.c",
        "src/mpcoll_ecb_pose.c",
        "src/mpcoll_floor.c",
        "src/mpcoll_floor_callbacks.c",
        "src/mpcoll_wall_ceil.c",
        "src/mpcoll_env.c",
        "src/match_flow.c",
        "src/locomotion.c",
        "src/locomotion_landing.c",
        "src/falcon_specials.c",
        "src/marth_specials.c",
        "src/sheik_specials.c",
        "src/zelda_specials.c",
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
        "src/combat_body.c",
        "src/combat_damageflyroll.c",
        "src/combat_shield.c",
        "src/items.c",
        "src/items_collision.c",
        "src/items_sheik.c",
        "src/items_spacies.c",
        "src/items_stage.c",
        "src/items_zelda.c",
    ],
    include_dirs=[get_numpy_include(), "src"],
    depends=DEPENDS,
    libraries=["m"],
    extra_compile_args=["-O3", "-Wall", "-Wextra", "-std=c11"],
)

setup(
    name="melee-sim-light",
    version="0.0.0",
    packages=[
        "melee_sim",
        "tools",
        "tools.extraction",
        # Data-only directories under tools/extraction: no .py files, but setuptools'
        # editable-install package discovery flags any undeclared directory containing
        # files as an ambiguous implicit namespace package unless listed here.
        "tools.extraction.source_artifacts",
        "tools.extraction.source_artifacts.attack_id.move_id",
        "tools.extraction.source_artifacts.motion_state.owners",
        "tools.extraction.source_artifacts.staling.move_id",
    ],
    package_data={
        "tools.extraction": [
            "source_artifacts/*.json",
            "source_artifacts/attack_id/move_id/*.bin",
            "source_artifacts/motion_state/owners/*.bin",
            "source_artifacts/motion_state/owners/*.json",
            "source_artifacts/staling/move_id/*.bin",
        ],
    },
    py_modules=["msl_binding"],
    ext_modules=[ext],
    cmdclass={"build_ext": _BuildExt, "build_py": _BuildPy},
)
