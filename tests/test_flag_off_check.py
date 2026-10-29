from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "build"))
import flag_off_check  # noqa: E402

CC = shutil.which("gcc") or shutil.which("cc")
OBJCOPY = shutil.which("objcopy")
needs_toolchain = pytest.mark.skipif(CC is None or OBJCOPY is None, reason="needs a C compiler and objcopy")

BASE_SOURCE = "int msl_test_value(int x) { return x * 3 + 1; }\n"
VIEWER_ONLY = BASE_SOURCE + "#if MSL_GX_RECORD\nint msl_test_draw(void) { return 7; }\n#endif\n"
CHANGED = "int msl_test_value(int x) { return x * 3 + 2; }\n"


def build_objects(directory: Path, sources: dict[str, str], *defines: str) -> Path:
    """Each source compiled the way a translation unit of the native build is (-O0 -g), then stripped as `run` strips it."""
    src = directory / "src"
    obj = directory / "obj"
    src.mkdir(parents=True)
    for name, text in sources.items():
        (src / name).write_text(text, encoding="utf-8")
        raw = directory / (name + ".o")
        subprocess.run([CC, "-O0", "-g", "-c", *defines, "-o", str(raw), name], cwd=src, check=True)
        out = obj / (Path(name).stem + ".o")
        out.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([OBJCOPY, "--strip-debug", str(raw), str(out)], check=True)
    return obj


@needs_toolchain
def test_viewer_only_code_leaves_the_objects_identical(tmp_path: Path) -> None:
    base = build_objects(tmp_path / "base", {"unit.c": BASE_SOURCE})
    head = build_objects(tmp_path / "head", {"unit.c": VIEWER_ONLY}, "-DMSL_GX_RECORD=0")
    unexpected, expected, same = flag_off_check.compare(base, head, set())
    assert (unexpected, expected, same) == ([], [], 1)


@needs_toolchain
def test_viewer_only_code_built_with_the_flag_is_a_difference(tmp_path: Path) -> None:
    base = build_objects(tmp_path / "base", {"unit.c": BASE_SOURCE})
    head = build_objects(tmp_path / "head", {"unit.c": VIEWER_ONLY}, "-DMSL_GX_RECORD=1")
    unexpected, _, _ = flag_off_check.compare(base, head, set())
    assert unexpected == ["unit.o"]


@needs_toolchain
def test_a_headless_change_fails_unless_it_is_expected(tmp_path: Path) -> None:
    base = build_objects(tmp_path / "base", {"unit.c": BASE_SOURCE, "other.c": "int msl_other;\n"})
    head = build_objects(tmp_path / "head", {"unit.c": CHANGED, "other.c": "int msl_other;\n"})
    assert flag_off_check.compare(base, head, set()) == (["unit.o"], [], 1)
    assert flag_off_check.compare(base, head, {"unit.o"}) == ([], ["unit.o"], 1)
    assert flag_off_check.report(*flag_off_check.compare(base, head, set()), objdump=None) == 1
    assert flag_off_check.report(*flag_off_check.compare(base, head, {"unit.o"}), objdump=None) == 0


@needs_toolchain
def test_an_object_on_one_side_only_is_a_difference(tmp_path: Path) -> None:
    base = build_objects(tmp_path / "base", {"unit.c": BASE_SOURCE})
    head = build_objects(tmp_path / "head", {"unit.c": BASE_SOURCE, "added.c": "int msl_added;\n"})
    assert flag_off_check.compare(base, head, set()) == (["added.o (only in head)"], [], 1)


def test_the_flag_defaults_off_in_every_build() -> None:
    compat = (ROOT / "src" / "platform" / "compat.h").read_text(encoding="utf-8")
    assert "#ifndef MSL_GX_RECORD\n#define MSL_GX_RECORD 0\n#endif" in compat
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    assert "MSL_GX_RECORD ?= 0" in makefile
    # The define reaches a command line only with the flag on.
    assert "ifeq ($(MSL_GX_RECORD),1)\nCPPFLAGS += -DMSL_GX_RECORD=1\nendif" in makefile


@pytest.mark.skipif(shutil.which("make") is None, reason="needs GNU make")
def test_the_makefile_refuses_other_values() -> None:
    result = subprocess.run(["make", "-n", "-C", str(ROOT), "native", "MSL_GX_RECORD=2"],
                            capture_output=True, text=True)
    assert result.returncode != 0
    assert "MSL_GX_RECORD must be 0 or 1" in result.stderr
