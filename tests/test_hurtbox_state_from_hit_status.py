from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import SEED_DTYPE


# Character ids (GALE01): Slippi post-frame `character`.
CHAR_FOX = 1
CHAR_FALCO = 22

# Stage ids (GALE01): Final Destination.
STAGE_FD = 32

# Common action ids (GALE01).
ACT_WAIT = 14

# src/hitboxes_tables.h (MSLHITB1 u16_6 bits)
HIT_GROUNDED = 1 << 9


def _find_nonzero_hit_status_entry(path: Path) -> tuple[int, int, int] | None:
    """Return (msid, frame, status) for any nonzero movescript hit-status entry."""
    with path.open("rb") as f:
        magic = f.read(8)
        if magic != b"MSLHSTA1":
            return None
        (ver, frame_count, reserved, entry_count) = struct.unpack("<IHHI", f.read(12))
        if int(ver) != 1 or int(reserved) != 0:
            return None
        if int(frame_count) <= 0 or int(entry_count) <= 0:
            return None

        index_recs: list[tuple[int, int]] = []
        for _ in range(int(entry_count)):
            (msid, _res, payload_bytes, payload_off) = struct.unpack("<HHII", f.read(12))
            if int(_res) != 0:
                return None
            if int(payload_bytes) != int(frame_count):
                return None
            index_recs.append((int(msid), int(payload_off)))

        # Prefer common invuln scripts (GALE01): EscapeN/EscapeF/EscapeB.
        prefer = [41, 42, 43]
        ordered = [rec for rec in index_recs if rec[0] in set(prefer)] + [
            rec for rec in index_recs if rec[0] not in set(prefer)
        ]

        for (msid, payload_off) in ordered:
            f.seek(payload_off)
            payload = f.read(int(frame_count))
            if len(payload) != int(frame_count):
                return None
            for frame in range(int(frame_count)):
                st = int(payload[frame])
                if st != 0:
                    return (msid, frame, st)

    return None


def _selected_body_hit_count(handle) -> int:
    import msl_binding

    _, count = msl_binding.debug_combat_select_body_hits(handle, 0, 16)
    return int(count)


@pytest.mark.integration
@pytest.mark.parametrize(
    "char_name,char_id",
    [
        ("fox", CHAR_FOX),
        ("falco", CHAR_FALCO),
    ],
)
def test_hit_status_table_gates_body_contact_selection(char_name: str, char_id: int) -> None:
    import msl_binding

    path = Path("data") / "hit_status" / f"{char_name}.bin"
    if not path.exists():
        pytest.skip(f"missing hit status artifact: {path}")

    found = _find_nonzero_hit_status_entry(path)
    if found is None:
        pytest.skip(f"no nonzero hit status entry found in: {path}")
    (msid, frame, status) = found
    assert int(status) != 0

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    assert seed_stride == SEED_DTYPE.itemsize

    seed = np.zeros((1,), dtype=SEED_DTYPE)
    seed["stage_id"][0] = np.uint32(STAGE_FD)
    seed["num_players"][0] = np.uint8(2)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["char_id"][0, :2] = np.uint8(char_id)
    seed["action_id"][0, :2] = np.uint16(ACT_WAIT)
    seed["facing"][0, :2] = np.uint8(1)
    seed["on_ground"][0, :2] = np.uint8(1)
    seed["ground_id"][0, :2] = np.uint16(0)
    seed["anim_frame_f32"][0, 1] = np.float32(float(int(frame)))
    seed["animation_index"][0, 1] = np.uint32(int(msid))

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = seed.view(np.uint8).reshape((1, seed_stride))
        msl_binding.reseed_seed(handle, seed_bytes)

        # Force overlapping primitives (BODY-only).
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(HIT_GROUNDED))
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

        # Override defender hit status to "normal": selection returns.
        msl_binding.debug_set_hit_status_override(handle, 0, 1, 0)
        assert _selected_body_hit_count(handle) == 1

        # Clear override: the table's nonzero status must block selection.
        msl_binding.debug_set_hit_status_override(handle, 0, 1, -1)
        # Decomp-first policy (GALE01):
        # - status==2 ("intangible") blocks body contact checks entirely.
        # - status==1 ("invincible") still allows contact checks, but collision does not apply
        #   damage/KB to the defender (handled elsewhere).
        if int(status) == 2:
            assert _selected_body_hit_count(handle) == 0
        else:
            assert _selected_body_hit_count(handle) == 1
    finally:
        msl_binding.destroy(handle)
        del handle
