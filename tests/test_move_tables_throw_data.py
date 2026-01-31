from __future__ import annotations

import json
from pathlib import Path

import pytest

CHAR_FOX = 1
CHAR_FALCO = 22

# GALE01 action ids: src/action_ids.h
ACT_THROW_F = 0x00DB
ACT_THROW_B = 0x00DC
ACT_THROW_HI = 0x00DD
ACT_THROW_LW = 0x00DE

pytestmark = pytest.mark.integration


@pytest.fixture(scope="module", autouse=True)
def _ensure_move_tables_loaded() -> None:
    try:
        import msl_binding
    except Exception:  # pragma: no cover
        pytest.skip("msl_binding extension not available")

    # Ensure batch init path runs move_tables_init().
    handle = msl_binding.init(batch_size=1, num_players=2)
    msl_binding.destroy(handle)


def _load_throw_expectations(move_path: Path, move_key: str) -> tuple[int, int, dict[int, dict]]:
    data = json.loads(move_path.read_text())
    events = data["moves"][move_key]["events"]

    # Decomp: set_throw_flags(hit_idx=0) sets throw_flags_b3 (release/apply throw hit),
    # while hit_idx=1 flips facing (throw_flags_b4).
    # refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4
    release_events = [
        ev
        for ev in events
        if ev.get("kind") == "set_throw_flags" and int(ev["data"]["hit_idx"]) == 0
    ]
    assert release_events
    release_frame = min(int(ev["frame"]) for ev in release_events)
    hit_idx = 0

    hitboxes: dict[int, dict] = {}
    for ev in events:
        if ev.get("kind") != "set_throw_hitbox":
            continue
        hb = ev["data"]
        hitboxes[int(hb["idx"])] = hb

    return int(release_frame), int(hit_idx), hitboxes


def _find_release_frame(msl_binding, char_id: int, throw_action_id: int) -> tuple[int, int]:
    assert int(msl_binding.move_tables_throw_has_release(char_id, throw_action_id)) == 1
    for f in range(0, 60):
        released, hit_idx = msl_binding.move_tables_throw_release_hit_idx(
            char_id, throw_action_id, float(f)
        )
        if int(released) == 1:
            return int(f), int(hit_idx)
    raise AssertionError("throw never released in [0,60)")


def test_move_tables_throwhi_release_diff_and_hitbox_params_match_json() -> None:
    import msl_binding

    root = Path(__file__).resolve().parents[1]
    fox_path = root / "data" / "moves" / "fox.json"
    falco_path = root / "data" / "moves" / "falco.json"
    if not fox_path.exists() or not falco_path.exists():
        pytest.skip("missing local data/moves/{fox,falco}.json (gitignored)")

    fox_release_frame, fox_hit_idx, fox_hitboxes = _load_throw_expectations(
        fox_path, "ftCo_SM_ThrowHi"
    )
    falco_release_frame, falco_hit_idx, falco_hitboxes = _load_throw_expectations(
        falco_path, "ftCo_SM_ThrowHi"
    )

    got_fox_frame, got_fox_hit_idx = _find_release_frame(msl_binding, CHAR_FOX, ACT_THROW_HI)
    got_falco_frame, got_falco_hit_idx = _find_release_frame(msl_binding, CHAR_FALCO, ACT_THROW_HI)

    assert got_fox_frame == fox_release_frame
    assert got_falco_frame == falco_release_frame
    assert got_fox_frame != got_falco_frame

    assert got_fox_hit_idx == fox_hit_idx
    assert got_falco_hit_idx == falco_hit_idx

    fox_params = msl_binding.move_tables_throw_hitbox_params(CHAR_FOX, ACT_THROW_HI, fox_hit_idx)
    assert fox_params is not None
    (dmg, angle, kbg, wsk, bkb, element, sfx_kind, sfx_severity) = fox_params
    exp = fox_hitboxes[fox_hit_idx]
    assert dmg == pytest.approx(float(exp["damage"]))
    assert int(angle) == int(exp["angle"])
    assert int(kbg) == int(exp["kbg"])
    assert int(wsk) == int(exp["wsk"])
    assert int(bkb) == int(exp["bkb"])
    assert int(element) == int(exp["element"])
    assert int(sfx_kind) == int(exp["sfx_kind"])
    assert int(sfx_severity) == int(exp["sfx_severity"])


@pytest.mark.parametrize(
    "char_id,throw_action_id,move_key",
    [
        (CHAR_FOX, ACT_THROW_F, "ftCo_SM_ThrowF"),
        (CHAR_FOX, ACT_THROW_B, "ftCo_SM_ThrowB"),
        (CHAR_FOX, ACT_THROW_LW, "ftCo_SM_ThrowLw"),
        (CHAR_FALCO, ACT_THROW_F, "ftCo_SM_ThrowF"),
        (CHAR_FALCO, ACT_THROW_B, "ftCo_SM_ThrowB"),
        (CHAR_FALCO, ACT_THROW_LW, "ftCo_SM_ThrowLw"),
    ],
)
def test_move_tables_other_throws_have_release_frame(char_id: int, throw_action_id: int, move_key: str) -> None:
    import msl_binding

    root = Path(__file__).resolve().parents[1]
    move_path = root / "data" / "moves" / ("fox.json" if char_id == CHAR_FOX else "falco.json")
    if not move_path.exists():
        pytest.skip(f"missing local data/moves/{move_path.name} (gitignored)")
    exp_release_frame, exp_hit_idx, _exp_hitboxes = _load_throw_expectations(move_path, move_key)

    got_release_frame, got_hit_idx = _find_release_frame(msl_binding, char_id, throw_action_id)
    assert got_release_frame == exp_release_frame
    assert got_hit_idx == exp_hit_idx
