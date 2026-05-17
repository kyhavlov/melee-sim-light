from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import SEED_DTYPE
from tools.extraction.extract_fighter_script_timeline import EVENT_IDS
from tools.slippi.known_data_artifacts import read_mslftsc1_v1


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
    """Return (msid, frame, status) for any nonzero MSLFTSC1 hit-status event."""
    table = read_mslftsc1_v1(path)
    by_msid = {
        int(entry.msid): table.events[entry.first_event : entry.first_event + entry.event_count]
        for entry in table.entries
    }
    prefer = [41, 42, 43]  # EscapeN/EscapeF/EscapeB.
    ordered_msids = prefer + [msid for msid in sorted(by_msid) if msid not in set(prefer)]
    for msid in ordered_msids:
        for ev in by_msid.get(msid, ()):
            if int(ev.kind_id) != EVENT_IDS["set_hit_status"]:
                continue
            status = int(ev.payload.get("state", 0))
            if status != 0:
                return (msid, int(ev.frame), status)
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

    path = Path("data") / "scripts" / f"{char_name}.bin"
    if not path.exists():
        pytest.skip(f"missing script timeline artifact: {path}")

    found = _find_nonzero_hit_status_entry(path)
    if found is None:
        pytest.skip(f"no nonzero hit status event found in: {path}")
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
