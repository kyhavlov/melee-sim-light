from __future__ import annotations

import os
from pathlib import Path
from types import SimpleNamespace

import numpy as np
import pyarrow as pa
import pytest

from tools.validation import validate_replay
from tools.validation.validate_replay import (
    NATIVE,
    NATIVE_BINARY,
    PPC_BINARY,
    QEMU,
    ReplayCase,
    load_native,
    load_suite_cases,
    run_cases,
    validate_one,
)


ROOT = Path(__file__).resolve().parents[1]
STARTER_REPLAY = (
    ROOT
    / "replays"
    / "validation"
    / "aggregate_recent"
    / "PutridJoyousOryx.slpz"
)
UCF_08 = dict(
    ucf_cardinals_1_0_enabled=False,
    ucf_shield_sdi_enabled=False,
    ucf_sdi_enabled=False,
    ucf_shield_drop_extended_enabled=False,
    ucf_shield_drop_084_enabled=False,
)


@pytest.mark.parametrize("replay,kind,lane,states,ignored", [
    ("sheik/PaleMajorEchidna.slpz", 97, "3", None, True),
    ("sheik/PaleMajorEchidna.slpz", 97, "2", None, False),
    ("yoshi/slippi-2025-06_Game_20250626T221245.slpz", 6, "1", None, True),
    ("yoshi/slippi-2025-06_Game_20250626T221245.slpz", 6, "0", None, False),
    ("ness/51444_Game_20250301T143614.slpz", 7, "1", (0, 4, 5, 11), True),
    ("ness/51444_Game_20250301T143614.slpz", 7, "1", (2,), False),
])
def test_item_misc_comparison_preserves_gameplay_lanes(
    monkeypatch, replay, kind, lane, states, ignored
) -> None:
    if not NATIVE.is_file() or not NATIVE_BINARY.is_file():
        pytest.skip("native core validation artifacts are unavailable")
    original_read = validate_replay._read_slippi

    def replace_field(array, name, replacement):
        return pa.StructArray.from_arrays(
            [replacement if field.name == name else array.field(field.name)
             for field in array.type], fields=array.type, mask=array.is_null())

    def read_with_changed_misc(*args):
        game = original_read(*args)
        items = game.frames.field("item")
        values = items.values
        selected = values.field("type").to_numpy() == kind
        if states is not None:
            selected &= np.isin(values.field("state").to_numpy(), states)
        assert np.any(selected)
        misc = values.field("misc")
        changed = misc.field(lane).to_numpy().copy()
        changed[selected] ^= 0xFF
        values = replace_field(values, "misc", replace_field(misc, lane, pa.array(changed)))
        items = pa.ListArray.from_arrays(items.offsets, values, type=items.type,
                                        mask=items.is_null())
        return SimpleNamespace(frames=replace_field(game.frames, "item", items),
                               start=game.start, metadata=game.metadata, end=game.end)

    monkeypatch.setattr(validate_replay, "_read_slippi", read_with_changed_misc)
    result = validate_one(load_native(), ROOT / "replays/validation" / replay,
                          frames=0, start_frame=None, timeout=20.0, backend="native")
    assert result["pass"] is ignored
    assert {field["field"].split("[")[0] for field in result["mismatch_fields"]} == (
        set() if ignored else {f"item.misc{lane}"})


def test_vanilla_magnifier_uses_leader_visibility() -> None:
    if not NATIVE.is_file() or not NATIVE_BINARY.is_file():
        pytest.skip("native core validation artifacts are unavailable")
    # The recorded code list has UCF 0.8 DB/SD/Tumble, without the newer hooks.
    # Missing raw C-stick inputs and render timing keep this diagnostic-only.
    result = validate_one(
        load_native(),
        ROOT / "replays/validation/icies/collision2024_ICs_v_DK_FD.slpz",
        frames=0, start_frame=None, timeout=30.0, backend="native",
        diagnostic=True,
        **UCF_08,
    )
    # Without the leader check Popo misses 1% at frame 2369, then knockback
    # diverges. Check the whole recording, not just that damage tick.
    assert result["pass"] is True
    assert result["matched_frames"] == result["total"] == 7570
    assert result["render_visibility_mismatch_count"] == 0
    assert result["diagnostic"] is True
    assert result["admitted"] is False


@pytest.mark.parametrize("replay,frame_count", [
    pytest.param("dk/diamond-diamond-33b572328d41bb20873121a4.slpz", 8869, id="squatrv"),
    pytest.param("icies/master-diamond-bf25cf805e23959b754c2c71.slpz", 12375, id="dashback"),
])
def test_ucf_08_matches_recorded_gameplay(replay, frame_count) -> None:
    if not NATIVE.is_file() or not NATIVE_BINARY.is_file():
        pytest.skip("native core validation artifacts are unavailable")
    # These anonymized online captures lack playedOn and raw C-stick bytes.
    result = validate_one(
        load_native(), ROOT / "replays/validation" / replay,
        frames=0, start_frame=None, timeout=30.0, backend="native",
        played_on="dolphin", diagnostic=True, **UCF_08,
    )
    assert result["pass"], result["details"]
    assert result["matched_frames"] == result["total"] == frame_count


@pytest.mark.parametrize("backend", ["ppc", "native"])
def test_validation_streams_starter_replay_from_arrow(backend: str) -> None:
    binary = NATIVE_BINARY if backend == "native" else PPC_BINARY
    required = [STARTER_REPLAY, NATIVE, binary]
    if backend == "ppc":
        required.append(QEMU)
    if not all(path.is_file() for path in required):
        pytest.skip(f"{backend} core validation artifacts are unavailable")

    result = validate_one(
        load_native(),
        # Preserve legacy .slp callers when storage has moved to .slpz.
        STARTER_REPLAY.with_suffix(".slp"),
        frames=0,
        start_frame=None,
        timeout=20.0,
        backend=backend,
    )

    assert result["pass"] is True
    assert result["frames"] == 7543
    assert result["available"] == 7543
    assert result["raw_frames"] == 7549
    assert result["seed_frame"] == -123
    assert result["first_ref_frame"] == -122
    assert result["matched_frames"] == 7543
    assert result["mismatched_frames"] == 0
    assert result["exact_prefix_frames"] == 7543
    assert result["strict_suffix_frames"] == 7543
    assert float(result["runner_seconds"]) > 0.0
    assert float(result["end_to_end_seconds"]) >= float(result["runner_seconds"])
    assert result["render_visibility_mismatch_count"] == 0
    assert result["first_render_visibility_mismatch_frame"] is None
    assert result["signed_zero_equal_count"] == 0
    assert result["first_mismatch_frame"] is None
    assert result["last_mismatch_frame"] is None
    assert result["mismatch_count"] == 0
    assert result["mismatch_fingerprint"] is None
    assert result["mismatch_fields"] == []
    assert result["details"] == []


def test_native_validation_replaces_interrupted_server(monkeypatch) -> None:
    if not all(path.is_file() for path in (STARTER_REPLAY, NATIVE, NATIVE_BINARY)):
        pytest.skip("native core validation artifacts are unavailable")

    runners = []

    def interrupt_first_job(native, replay, **kwargs):
        runner = kwargs["runner"]
        runners.append(runner)
        if len(runners) == 1:
            # Leave an incomplete header in the pipe, as a write timeout can.
            # Inject the interruption so the test doesn't depend on CPU speed.
            os.write(runner.stdin_fd, b"\x00")
            raise TimeoutError("interrupted job header")
        return validate_one(native, replay, **kwargs)

    monkeypatch.setattr(validate_replay, "validate_one", interrupt_first_job)
    case = ReplayCase(STARTER_REPLAY, str(STARTER_REPLAY))
    outcomes, _ = run_cases(
        load_native(),
        [case, case],
        workers=1,
        frames=16,
        start_frame=None,
        timeout=30.0,
        backend="native",
        signed_zero_equal=False,
    )

    assert outcomes[0].error == "TimeoutError: interrupted job header"
    assert outcomes[1].error is None
    assert outcomes[1].result["pass"] is True
    assert outcomes[1].result["frames"] == 16
    assert runners[0].process.pid != runners[1].process.pid
    assert all(runner.process.poll() is not None for runner in runners)


def test_native_validation_runs_current_oracle_replays_in_parallel() -> None:
    required = [NATIVE, NATIVE_BINARY]
    if not all(path.is_file() for path in required):
        pytest.skip("native core validation artifacts are unavailable")
    _suite, cases = load_suite_cases(
        ROOT / "replays/suites/aggregate_recent.json",
        characters=frozenset(("fox",)),
        stages=frozenset((32,)),
    )

    outcomes, wall_seconds = run_cases(
        load_native(),
        cases,
        # Bound immutable data copies on memory-limited CI runners.
        workers=min(4, len(cases)),
        frames=256,
        start_frame=None,
        timeout=3.0,
        backend="native",
        signed_zero_equal=False,
    )

    assert len(outcomes) == 3
    assert all(outcome.error is None for outcome in outcomes)
    assert all(outcome.result is not None and outcome.result["pass"] for outcome in outcomes)
    assert wall_seconds < 3.0
