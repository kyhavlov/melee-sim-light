from __future__ import annotations

import json
import math
from pathlib import Path

import pytest

CHAR_FOX = 1
CHAR_FALCO = 22
CHAR_SHEIK = 7
CHAR_MARTH = 18

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


def _load_throw_cmd1_window(move_path: Path, move_key: str) -> tuple[int, int]:
    data = json.loads(move_path.read_text())
    events = data["moves"][move_key]["events"]

    on_frame = None
    off_frame = None
    for ev in events:
        if ev.get("kind") != "set_cmd_var":
            continue
        payload = ev.get("data") or {}
        if int(payload.get("idx", -1)) != 1:
            continue
        frame = int(ev["frame"])
        value = int(payload.get("value", 0))
        if value == 1:
            if on_frame is None or frame < on_frame:
                on_frame = frame
        elif on_frame is not None and frame >= on_frame:
            if off_frame is None or frame < off_frame:
                off_frame = frame

    assert on_frame is not None
    if off_frame is None:
        off_frame = 10_000
    return int(on_frame), int(off_frame)


def _load_throw_projectile_pulses(move_path: Path, move_key: str) -> list[int]:
    data = json.loads(move_path.read_text())
    events = data["moves"][move_key]["events"]
    frames = sorted(
        {
            int(ev["frame"])
            for ev in events
            if ev.get("kind") == "set_throw_spawn_projectile"
        }
    )
    assert frames
    return frames


def _load_create_before_release(move_path: Path, move_key: str) -> bool:
    data = json.loads(move_path.read_text())
    events = data["moves"][move_key]["events"]
    release_frames = [
        int(ev["frame"])
        for ev in events
        if ev.get("kind") == "set_throw_flags" and int(ev["data"]["hit_idx"]) == 0
    ]
    assert release_frames
    release_frame = min(release_frames)
    create_frames = [int(ev["frame"]) for ev in events if ev.get("kind") == "create_hitbox"]
    return bool(create_frames and min(create_frames) < release_frame)


def _move_path_for_char(root: Path, char_id: int) -> Path:
    names = {
        CHAR_FOX: "fox.json",
        CHAR_FALCO: "falco.json",
        CHAR_SHEIK: "sheik.json",
        CHAR_MARTH: "marth.json",
    }
    return root / "data" / "moves" / names[char_id]


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


@pytest.mark.parametrize(
    "char_id,throw_action_id,move_key",
    [
        (CHAR_FOX, ACT_THROW_F, "ftCo_SM_ThrowF"),
        (CHAR_FOX, ACT_THROW_B, "ftCo_SM_ThrowB"),
        (CHAR_FALCO, ACT_THROW_F, "ftCo_SM_ThrowF"),
        (CHAR_FALCO, ACT_THROW_B, "ftCo_SM_ThrowB"),
    ],
)
def test_move_tables_throw_release_frame_valid_actions(char_id: int, throw_action_id: int, move_key: str) -> None:
    import msl_binding

    root = Path(__file__).resolve().parents[1]
    move_path = root / "data" / "moves" / ("fox.json" if char_id == CHAR_FOX else "falco.json")
    if not move_path.exists():
        pytest.skip(f"missing local data/moves/{move_path.name} (gitignored)")

    exp_release_frame, _exp_hit_idx, _exp_hitboxes = _load_throw_expectations(move_path, move_key)
    ok, release_af = msl_binding.move_tables_throw_release_frame(char_id, throw_action_id)
    assert int(ok) == 1
    assert math.isfinite(float(release_af))
    assert float(release_af) == pytest.approx(float(exp_release_frame), abs=1e-6)


def test_move_tables_throw_release_frame_invalid_action_returns_0() -> None:
    import msl_binding

    invalid_action = 0x0020  # not a Throw* GALE01 action id
    ok, release_af = msl_binding.move_tables_throw_release_frame(CHAR_FOX, invalid_action)
    assert int(ok) == 0
    assert float(release_af) == pytest.approx(0.0, abs=0.0)

@pytest.mark.parametrize(
    "char_id,throw_action_id,move_key",
    [
        (CHAR_FOX, ACT_THROW_B, "ftCo_SM_ThrowB"),
        (CHAR_FOX, ACT_THROW_HI, "ftCo_SM_ThrowHi"),
        (CHAR_FOX, ACT_THROW_LW, "ftCo_SM_ThrowLw"),
        (CHAR_FALCO, ACT_THROW_B, "ftCo_SM_ThrowB"),
        (CHAR_FALCO, ACT_THROW_HI, "ftCo_SM_ThrowHi"),
        (CHAR_FALCO, ACT_THROW_LW, "ftCo_SM_ThrowLw"),
    ],
)
def test_move_tables_throw_cmd1_active_matches_json(
    char_id: int, throw_action_id: int, move_key: str
) -> None:
    import msl_binding

    root = Path(__file__).resolve().parents[1]
    move_path = root / "data" / "moves" / ("fox.json" if char_id == CHAR_FOX else "falco.json")
    if not move_path.exists():
        pytest.skip(f"missing local data/moves/{move_path.name} (gitignored)")

    on_frame, off_frame = _load_throw_cmd1_window(move_path, move_key)
    for f in range(0, 80):
        expected = 1 if (f >= on_frame and f < off_frame) else 0
        got = int(msl_binding.move_tables_throw_cmd1_active(char_id, throw_action_id, float(f)))
        assert got == expected, (
            f"{move_key} frame={f} expected cmd1_active={expected}, got={got}"
        )


@pytest.mark.parametrize(
    "char_id,throw_action_id,move_key",
    [
        (CHAR_FOX, ACT_THROW_B, "ftCo_SM_ThrowB"),
        (CHAR_FOX, ACT_THROW_HI, "ftCo_SM_ThrowHi"),
        (CHAR_FOX, ACT_THROW_LW, "ftCo_SM_ThrowLw"),
        (CHAR_FALCO, ACT_THROW_B, "ftCo_SM_ThrowB"),
        (CHAR_FALCO, ACT_THROW_HI, "ftCo_SM_ThrowHi"),
        (CHAR_FALCO, ACT_THROW_LW, "ftCo_SM_ThrowLw"),
    ],
)
def test_move_tables_throw_projectile_pulses_match_json(
    char_id: int, throw_action_id: int, move_key: str
) -> None:
    import msl_binding

    root = Path(__file__).resolve().parents[1]
    move_path = root / "data" / "moves" / ("fox.json" if char_id == CHAR_FOX else "falco.json")
    if not move_path.exists():
        pytest.skip(f"missing local data/moves/{move_path.name} (gitignored)")

    pulse_frames = set(_load_throw_projectile_pulses(move_path, move_key))
    for cur in range(1, 80):
        prev = cur - 1
        expected = 1 if cur in pulse_frames else 0
        got = int(
            msl_binding.move_tables_throw_should_spawn_projectile(
                char_id, throw_action_id, float(prev), float(cur)
            )
        )
        assert got == expected, (
            f"{move_key} prev={prev} cur={cur} expected spawn={expected}, got={got}"
        )


@pytest.mark.parametrize(
    "char_id,throw_action_id,move_key",
    [
        (CHAR_SHEIK, ACT_THROW_LW, "ftCo_SM_ThrowLw"),
        (CHAR_FOX, ACT_THROW_LW, "ftCo_SM_ThrowLw"),
        (CHAR_FALCO, ACT_THROW_LW, "ftCo_SM_ThrowLw"),
        (CHAR_MARTH, ACT_THROW_LW, "ftCo_SM_ThrowLw"),
    ],
)
def test_move_tables_throw_release_after_create_hitbox_matches_json(
    char_id: int, throw_action_id: int, move_key: str
) -> None:
    import msl_binding

    root = Path(__file__).resolve().parents[1]
    move_path = _move_path_for_char(root, char_id)
    if not move_path.exists():
        pytest.skip(f"missing local data/moves/{move_path.name} (gitignored)")

    expected = 1 if _load_create_before_release(move_path, move_key) else 0
    got = int(msl_binding.move_tables_throw_release_after_create_hitbox(char_id, throw_action_id))
    assert got == expected
    assert got == (1 if char_id == CHAR_SHEIK else 0)
