from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import read_dataset


def _read_mslacid1_v2_x4_flags_low_bytes(path: Path) -> np.ndarray:
    buf = path.read_bytes()
    assert buf[:8] == b"MSLACID1"
    (ver,) = struct.unpack_from("<I", buf, 8)
    assert ver == 2
    (count,) = struct.unpack_from("<H", buf, 12)
    (flags_off,) = struct.unpack_from("<I", buf, 20)
    assert flags_off + int(count) * 4 <= len(buf)
    lows = np.frombuffer(buf, dtype="<u4", offset=int(flags_off), count=int(count)) & np.uint32(0xFF)
    return lows.astype(np.uint8, copy=False)


@pytest.mark.integration
def test_suite_observed_actions_do_not_require_ft_800895E0_rewrite_paths() -> None:
    """
    Guard against unmodeled ft_800895E0 rewrite paths.

    ft_800895E0 uses (u8)MotionState.x4_flags as a compare key against fp+0x2073, but has two
    special-case rewrites that replace the x2070 word before writing fp->x2070:
    - If fp->kind == 0x11 and flags_low == 0x71, it overwrites the word with 0x240063.
    - If flags_low == 0x62 and it_8026B6C8(fp->x1974) is true, it overwrites the word with 0x44003D.
    refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0

    The simulator currently does not model the required fighter-kind/item-pointer state for these
    rewrites, so enforce that suite-observed action_ids for Fox/Falco never use x4_flags low bytes
    0x71 or 0x62.
    """

    ds_root = Path("datasets/fox_falco_fd_ucf084_recent")
    if not ds_root.exists():
        pytest.skip("missing local preprocessed datasets for suite (datasets/ is gitignored)")

    msl_paths = sorted(ds_root.rglob("*.msl"))
    if not msl_paths:
        pytest.skip("no .msl files found under datasets/fox_falco_fd_ucf084_recent")

    fox_low = _read_mslacid1_v2_x4_flags_low_bytes(Path("data/attack_id/move_id/fox.bin"))
    falco_low = _read_mslacid1_v2_x4_flags_low_bytes(Path("data/attack_id/move_id/falco.bin"))

    bad: set[tuple[int, int, int]] = set()

    for p in msl_paths:
        try:
            ds = read_dataset(str(p))
        except ValueError as e:
            # Dataset caches live under datasets/ and are gitignored. When the seed schema changes,
            # local caches can become stale and fail the record_size check in read_dataset.
            pytest.skip(f"stale local dataset cache (rerun preprocess_suite --force): {e}")
        s = ds.samples
        for field in ("seed_t", "ref_t1"):
            action = s[field]["action_id"][:, :2]
            char = s[field]["char_id"][:, :2]

            for i in range(action.shape[0]):
                for pl in range(2):
                    cid = int(char[i, pl])
                    aid = int(action[i, pl])
                    if cid == 1:
                        lb = int(fox_low[aid]) if 0 <= aid < int(fox_low.shape[0]) else 0
                    elif cid == 22:
                        lb = int(falco_low[aid]) if 0 <= aid < int(falco_low.shape[0]) else 0
                    else:
                        continue
                    if lb in (0x71, 0x62):
                        bad.add((cid, aid, lb))

    if bad:
        msg = ", ".join(
            [f"(char={cid}, action_id={aid}, flags_low=0x{lb:02x})" for (cid, aid, lb) in sorted(bad)]
        )
        raise AssertionError(
            "suite contains action_ids with MotionState.x4_flags low byte requiring ft_800895E0 rewrite paths: "
            + msg
        )
