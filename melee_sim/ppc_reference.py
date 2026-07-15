from __future__ import annotations

import subprocess
import tempfile
from pathlib import Path

import numpy as np

from . import dtypes
from .controller import BUTTON_X


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def ppc_binary() -> Path:
    return _repo_root() / "build" / "melee_core" / "ppc" / "melee-core-ppc"


def ppc_qemu() -> Path:
    return (
        _repo_root()
        / "build"
        / "melee_core"
        / "toolchain"
        / "root"
        / "usr"
        / "bin"
        / "qemu-ppc-static"
    )


def ppc_qemu_sysroot() -> Path:
    return (
        _repo_root()
        / "build"
        / "melee_core"
        / "toolchain"
        / "root"
        / "usr"
        / "powerpc-linux-gnu"
    )


def build_ppc_reference(*, jobs: int = 2) -> Path:
    """Build the PPC reference without invoking the production extension build."""

    root = _repo_root()
    subprocess.run(
        ["make", "-f", "src/melee_core/Makefile", f"-j{max(1, int(jobs))}", "runtime"],
        cwd=root,
        check=True,
    )
    return ppc_binary()


def _one_row(value: np.ndarray, dtype: np.dtype, name: str) -> np.ndarray:
    row = np.asarray(value, dtype=dtype)
    if row.size != 1:
        raise ValueError(f"{name} must contain exactly one row")
    return np.ascontiguousarray(row.reshape(1))


def run_ppc_reference(
    config: np.ndarray,
    inputs: np.ndarray,
    *,
    previous_input: np.ndarray | None = None,
    data_dir: Path | str | None = None,
    binary: Path | str | None = None,
    timeout: float = 60.0,
) -> np.ndarray:
    """Run one free-running input tape in one QEMU process and return MslCoreCompare rows."""

    root = _repo_root()
    executable = Path(binary) if binary is not None else ppc_binary()
    qemu = ppc_qemu()
    sysroot = ppc_qemu_sysroot()
    game_data = Path(data_dir) if data_dir is not None else root / "refs" / "melee-disc" / "files"
    if not executable.is_file():
        raise FileNotFoundError(
            f"PPC reference is missing; run build_ppc_reference(): {executable}"
        )
    if not qemu.is_file():
        raise FileNotFoundError(f"repository-local qemu is missing: {qemu}")

    config_row = _one_row(config, dtypes.match_config_dtype(), "config")
    input_rows = np.ascontiguousarray(np.asarray(inputs, dtype=dtypes.input_dtype()).reshape(-1))
    previous_row = (
        None
        if previous_input is None
        else _one_row(previous_input, dtypes.input_dtype(), "previous_input")
    )

    with tempfile.TemporaryDirectory(prefix="msl-ppc-reference-") as tmp:
        tmp_dir = Path(tmp)
        config_path = tmp_dir / "config.bin"
        previous_path = tmp_dir / "previous.bin"
        input_path = tmp_dir / "input.bin"
        output_path = tmp_dir / "compare.bin"
        config_row.tofile(config_path)
        input_rows.tofile(input_path)
        previous_arg = "-"
        if previous_row is not None:
            previous_row.tofile(previous_path)
            previous_arg = str(previous_path)

        subprocess.run(
            [
                str(qemu),
                "-L",
                str(sysroot),
                str(executable),
                str(game_data),
                str(config_path),
                previous_arg,
                str(input_path),
                str(output_path),
            ],
            cwd=root,
            check=True,
            timeout=timeout,
        )
        raw = output_path.read_bytes()

    compare_dtype = dtypes.compare_dtype()
    expected_size = input_rows.size * compare_dtype.itemsize
    if len(raw) != expected_size:
        raise RuntimeError(
            f"PPC reference emitted {len(raw)} bytes; expected {expected_size} "
            f"for {input_rows.size} compare rows"
        )
    return np.frombuffer(raw, dtype=compare_dtype).copy()


def fox_fd_config(*, frame_id: int = -1, random_seed: int = 1) -> np.ndarray:
    config = np.zeros(1, dtype=dtypes.match_config_dtype())
    config["stage_id"] = np.uint32(32)
    config["frame_id"] = np.int32(frame_id)
    config["frame_pre_random_seed"] = np.uint32(random_seed)
    config["num_players"] = np.uint8(2)
    config["stock_count"] = np.uint8(4)
    config["players"][0, 0]["char_id"] = np.uint8(1)
    config["players"][0, 1]["char_id"] = np.uint8(1)
    return config


def locomotion_tape() -> np.ndarray:
    """Bounded tape covering neutral, ground movement, jump, fall, and landing."""

    chunks: list[np.ndarray] = []

    def add(frames: int, *, main_x: int = 0, main_y: int = 0, buttons: int = 0) -> None:
        chunk = np.zeros(frames, dtype=dtypes.input_dtype())
        chunk["p"]["main_x"][:, 0] = np.int8(main_x)
        chunk["p"]["main_y"][:, 0] = np.int8(main_y)
        chunk["p"]["buttons"][:, 0] = np.uint16(buttons)
        chunks.append(chunk)

    add(8)
    add(30, main_x=32)
    add(8)
    add(1, main_x=80)
    add(35, main_x=80)
    add(10, main_x=-80)
    add(12)
    add(1, main_x=45, buttons=BUTTON_X)
    add(75, main_x=45)
    add(20)
    return np.concatenate(chunks)
