from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _run_one_step(dataset_path: Path, record: int):
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    assert int(row.shape[0]) == 1

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = row["seed_t"].view("u1").reshape(1, seed_stride).copy()
    prev_input_bytes = row["prev_input_t"].view("u1").reshape(1, input_stride).copy()
    input_bytes = row["input_t"].view("u1").reshape(1, input_stride).copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        ref = row["ref_t1"][0].copy()
        return out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_damage_on_every_hitlag_sdi_target_pm1_rows_are_replay_exact() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    rows = (313, 314, 315)
    p = 0
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for record={rec}"

    target = samples[314 : 315]
    assert int(target["seed_t"]["action_id"][0, p]) == 79
    assert int(target["seed_t"]["hitlag"][0, p]) == 5
    assert int(target["ref_t1"]["hitlag"][0, p]) == 4
    assert float(target["seed_t"]["pos_x"][0, p]) == pytest.approx(-17.902629852294922, abs=1e-6)
    assert float(target["ref_t1"]["pos_x"][0, p]) == pytest.approx(-23.902629852294922, abs=1e-6)

    for rec in rows:
        out, ref = _run_one_step(dataset_path, rec)
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damage_on_every_hitlag_sdi_explicit_negative_control_row_3097_stays_exact() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[3097 : 3098]
    p = 1
    assert int(row["seed_t"]["action_id"][0, p]) == 86
    assert int(row["seed_t"]["hitlag"][0, p]) == 4
    assert int(row["ref_t1"]["hitlag"][0, p]) == 3
    assert float(row["seed_t"]["pos_x"][0, p]) == pytest.approx(float(row["ref_t1"]["pos_x"][0, p]), abs=1e-6)

    out, ref = _run_one_step(dataset_path, 3097)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damageflylw_hitlag_sdi_target_pm1_and_negative_control_are_replay_exact() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    rows = (3374, 3375, 3376)
    neg = 3373
    p = 1
    for rec in (*rows, neg):
        assert int(samples.shape[0]) > rec, f"replay too short for record={rec}"

    target = samples[3375 : 3376]
    assert int(target["seed_t"]["action_id"][0, p]) == 89
    assert int(target["seed_t"]["hitlag"][0, p]) == 6
    assert float(target["seed_t"]["pos_x"][0, p]) == pytest.approx(-81.57569122314453, abs=1e-6)
    assert float(target["ref_t1"]["pos_x"][0, p]) == pytest.approx(-87.57569122314453, abs=1e-6)

    for rec in rows:
        out, ref = _run_one_step(dataset_path, rec)
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    out_neg, ref_neg = _run_one_step(dataset_path, neg)
    assert int(out_neg["action_id"][p]) == int(ref_neg["action_id"][p])
    assert int(out_neg["hitlag"][p]) == int(ref_neg["hitlag"][p])
    assert float(out_neg["pos_x"][p]) == pytest.approx(float(ref_neg["pos_x"][p]), abs=1e-6)


@pytest.mark.integration
def test_downdamaged_hitlag_sdi_target_pm1_and_negative_control_are_replay_exact() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    rows = (6049, 6050, 6051)
    neg = 6048
    p = 1
    for rec in (*rows, neg):
        assert int(samples.shape[0]) > rec, f"replay too short for record={rec}"

    target = samples[6050 : 6051]
    assert int(target["seed_t"]["action_id"][0, p]) == 193
    assert int(target["seed_t"]["hitlag"][0, p]) == 2
    assert float(target["seed_t"]["pos_x"][0, p]) == pytest.approx(17.695518493652344, abs=1e-6)
    assert float(target["ref_t1"]["pos_x"][0, p]) == pytest.approx(23.695518493652344, abs=1e-6)

    for rec in rows:
        out, ref = _run_one_step(dataset_path, rec)
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
        if rec != 6051:
            assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    out_neg, ref_neg = _run_one_step(dataset_path, neg)
    assert int(out_neg["action_id"][p]) == int(ref_neg["action_id"][p])
    assert int(out_neg["hitlag"][p]) == int(ref_neg["hitlag"][p])
    assert float(out_neg["pos_x"][p]) == pytest.approx(float(ref_neg["pos_x"][p]), abs=1e-6)


@pytest.mark.integration
def test_damageflyhi_hitlag_sdi_target_pm1_and_negative_control_are_replay_exact() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "QuerulousGrandDinosaur.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    rows = (4158, 4159, 4160)
    neg = 4162
    p = 0
    for rec in (*rows, neg):
        assert int(samples.shape[0]) > rec, f"replay too short for record={rec}"

    target = samples[4159 : 4160]
    assert int(target["seed_t"]["action_id"][0, p]) == 87
    assert int(target["seed_t"]["hitlag"][0, p]) == 5
    assert float(target["seed_t"]["pos_x"][0, p]) == pytest.approx(39.57304000854492, abs=1e-6)
    assert float(target["ref_t1"]["pos_x"][0, p]) == pytest.approx(37.62303924560547, abs=1e-6)
    assert float(target["ref_t1"]["pos_y"][0, p]) == pytest.approx(6.16510009765625, abs=1e-6)

    for rec in rows:
        out, ref = _run_one_step(dataset_path, rec)
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    out_neg, ref_neg = _run_one_step(dataset_path, neg)
    assert int(out_neg["action_id"][p]) == int(ref_neg["action_id"][p])
    assert int(out_neg["hitlag"][p]) == int(ref_neg["hitlag"][p])
    assert int(out_neg["hitstun"][p]) == int(ref_neg["hitstun"][p])
    assert float(out_neg["pos_x"][p]) == pytest.approx(float(ref_neg["pos_x"][p]), abs=1e-6)
    assert float(out_neg["pos_y"][p]) == pytest.approx(float(ref_neg["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_damageflytop_hitlag_sdi_target_and_neighbors_are_replay_exact() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "replays/validation/cardinal_1.0_recent/"
        "AttachedGoodNaturedGuanaco.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    rows = (612, 613, 614)
    p = 1
    for rec in rows:
        assert int(samples.shape[0]) > rec, f"replay too short for record={rec}"

    target = samples[613 : 614]
    assert int(target["seed_t"]["action_id"][0, p]) == 90
    assert int(target["seed_t"]["hitlag"][0, p]) == 4
    assert float(target["seed_t"]["pos_x"][0, p]) == pytest.approx(8.924493789672852, abs=1e-6)
    assert float(target["seed_t"]["pos_y"][0, p]) == pytest.approx(15.222575187683105, abs=1e-6)
    assert float(target["ref_t1"]["pos_x"][0, p]) == pytest.approx(12.974493980407715, abs=1e-6)
    assert float(target["ref_t1"]["pos_y"][0, p]) == pytest.approx(19.572574615478516, abs=1e-6)

    for rec in rows:
        out, ref = _run_one_step(dataset_path, rec)
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_first_active_hitlag_radius_crossing_sdi_applies_ppa_2185() -> None:
    # Replay-real lock for a first active-hitlag SDI pulse where damage entry reset the
    # replay-visible x670/x671 seeds to 0xFE, but callback-time vanilla still consumes the
    # current stick after it newly crosses the ftCo_Damage_OnEveryHitlag radius gate.
    #
    # Dolphin probe: reports/triage/damage_sdi_probe/ppa.jsonl (local scratch)
    #   frame 2063: lstick=(0.975,0), lstick1=(0.475,0), pos_x +5.85
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "replays/validation/aggregate_recent/"
        "PriceyPartialAlbatross.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[2185:2186]
    p = 0
    assert int(row["seed_t"]["action_id"][0, p]) == 78
    assert int(row["seed_t"]["hitlag"][0, p]) == 4
    assert int(row["seed_t"]["tilt_timer_x"][0, p]) == 0xFE
    assert float(row["ref_t1"]["pos_x"][0, p] - row["seed_t"]["pos_x"][0, p]) == pytest.approx(
        5.8499999, abs=1e-5
    )

    out, ref = _run_one_step(dataset_path, 2185)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_held_radius_stick_does_not_retrigger_first_hitlag_sdi_prh_659() -> None:
    # Negative for the radius-crossing bridge: PRH has active hitlag and a high-magnitude stick,
    # but callback-time vanilla already had a high-magnitude lstick1 from the previous frame and
    # applies no new SDI displacement.
    #
    # Dolphin probe: reports/triage/damage_sdi_probe/prh.jsonl (local scratch)
    #   frame 537: lstick=(0,-0.9875), lstick1=(0,-0.9875), pos unchanged
    root = Path(__file__).resolve().parents[1]
    dataset_rel = (
        "replays/validation/aggregate_recent/"
        "PositiveRevolvingHyena.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[659:660]
    p = 0
    assert int(row["seed_t"]["action_id"][0, p]) == 90
    assert int(row["seed_t"]["hitlag"][0, p]) == 7
    assert int(row["seed_t"]["tilt_timer_x"][0, p]) == 0xFE
    assert int(row["seed_t"]["tilt_timer_y"][0, p]) == 0xFE
    assert float(row["ref_t1"]["pos_x"][0, p]) == pytest.approx(
        float(row["seed_t"]["pos_x"][0, p]), abs=1e-6
    )
    assert float(row["ref_t1"]["pos_y"][0, p]) == pytest.approx(
        float(row["seed_t"]["pos_y"][0, p]), abs=1e-6
    )

    out, ref = _run_one_step(dataset_path, 659)
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)
