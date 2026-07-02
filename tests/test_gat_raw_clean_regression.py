from __future__ import annotations

import csv
import os
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _dataset_path(root: Path) -> Path:
    rel = (
        "replays/validation/cardinal_1.0_recent/"
        "GracefulAttachedTurtle.slpz"
    )
    path = root / rel
    if not path.exists():
        pytest.skip(f"missing local replay: {rel}")
    return path


def _row_bytes(samples: np.ndarray, record: int, field: str, stride: int) -> np.ndarray:
    return (
        np.frombuffer(samples[record : record + 1][field].tobytes(order="C"), dtype=np.uint8)
        .copy()
        .reshape(1, stride)
    )


def _rollout_row(dataset_path: Path, start_record: int, end_record: int, *, trace_path: Path | None = None) -> np.void:
    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    prev_trace_env = os.environ.get("MSL_RNG_TRACE_PATH")
    if trace_path is not None:
        os.environ["MSL_RNG_TRACE_PATH"] = str(trace_path)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, _row_bytes(samples, start_record, "seed_t", seed_stride))
        for record in range(start_record, end_record + 1):
            binding.step_input(
                handle,
                _row_bytes(samples, record, "prev_input_t", input_stride),
                _row_bytes(samples, record, "input_t", input_stride),
            )
            binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
        if trace_path is not None:
            if prev_trace_env is None:
                os.environ.pop("MSL_RNG_TRACE_PATH", None)
            else:
                os.environ["MSL_RNG_TRACE_PATH"] = prev_trace_env
    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()


def _trace_site_count(trace_path: Path, *, start_record: int, record: int, site_id: int) -> int:
    total = 0
    with trace_path.open("r", encoding="utf-8") as fh:
        reader = csv.DictReader(fh, delimiter="\t")
        for row in reader:
            if int(row["site_id"]) == int(site_id) and start_record + int(row["step"]) == int(record):
                total += int(row["call_count"])
    return total


@pytest.mark.integration
def test_gat_cliffattack_x221d_b5_suppresses_self_nudge_at_8226() -> None:
    # Continuous CliffClimb/CliffAttack/CliffEscape options carry fp->x221D_b5, and
    # ftCommon_8007E0E4 skips the self ftCommon_8007DD7C overlap nudge while that bit is live.
    # GAT 8226 is a source-positive CliffAttackQuick row; without the hidden-bit owner, P1 gets a
    # false +0.3 xF8_playerNudgeVel.x displacement that later makes SideB miss the floor.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_8009AEA4
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007E0E4,ftCommon_8007DD7C}
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset_path(root)
    ds = load_replay_buffers(str(dataset_path))
    record = 8226
    p = 1

    seed = ds.rows[record]["seed_t"]
    ref = ds.rows[record]["ref_t1"]
    assert int(seed["action_id"][p]) == 257  # CliffAttackQuick
    assert int(seed["seed_prev_action_id"][p]) == 257

    out = _rollout_row(dataset_path, record, record)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert np.float32(out["pos_x"][p]).view(np.uint32) == np.float32(ref["pos_x"][p]).view(np.uint32)


@pytest.mark.integration
def test_gat_landingairlw_downattacku_damageflyroll_pre_gate_rng_owner_at_11134() -> None:
    # Runtime DamageFlyRoll source owner:
    # - Fighter_8006CDA4 runs one hidden pre-gate HSD_Randi for this LandingAirLw phase.
    # - Runtime admission requires a current ProcessHit source from an authored DownAttackU
    #   HitCapsule; adjacent rows stay cold.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # data/moves/{fox,falco}.json::moves.ftCo_SM_DownAttackU.events.create_hitbox
    root = Path(__file__).resolve().parents[1]
    dataset_path = _dataset_path(root)
    ds = load_replay_buffers(str(dataset_path))
    target = 11134
    victim = 1
    attacker = 0
    trace_path = root / "reports/triage/gat_11134_test_rng.tsv"

    seed = ds.rows[target]["seed_t"]
    ref = ds.rows[target]["ref_t1"]
    assert int(seed["action_id"][victim]) == 74  # LandingAirLw
    assert int(seed["fighter_8006cda4_pre_gate_consume_count"][victim]) == 1
    assert int(seed["action_id"][attacker]) == 187  # DownAttackU
    assert int(ref["action_id"][victim]) == 91  # DamageFlyRoll

    out = _rollout_row(dataset_path, 11011, target, trace_path=trace_path)
    assert int(out["action_id"][victim]) == 91
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim])
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim])
    assert _trace_site_count(trace_path, start_record=11011, record=target, site_id=5) == 1
    assert _trace_site_count(trace_path, start_record=11011, record=target, site_id=1) == 1
    assert _trace_site_count(trace_path, start_record=11011, record=target - 1, site_id=5) == 0
    assert _trace_site_count(trace_path, start_record=11011, record=target + 1, site_id=5) == 0
