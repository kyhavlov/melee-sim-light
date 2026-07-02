from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _one_step_out_compare(*, ds, row) -> np.ndarray:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            5088,
            1,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            6012,
            1,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.slpz",
            815,
            1,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz",
            2259,
            1,
        ),
    ],
)
def test_same_frame_damage_does_not_reset_last_attack_landed(dataset_rel: str, record: int, p: int) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > record, f"replay too short for regression check: num_records={int(samples.shape[0])}"

    row = samples[record : record + 1]

    # Replay-real preconditions for the decomp-ordering lock:
    # - the attacker is already carrying a concrete landed attack id at seed,
    # - the sim previously rewrote t+1 to FtMoveId_Default (1) after same-frame damage-state entry.
    assert int(row["seed_t"]["last_attack_landed"][0, p]) != 0
    assert int(row["ref_t1"]["last_attack_landed"][0, p]) != 1

    out = _one_step_out_compare(ds=ds, row=row)
    got_last_attack = int(out["last_attack_landed"][0, p])
    exp_last_attack = int(row["ref_t1"]["last_attack_landed"][0, p])
    assert got_last_attack == exp_last_attack, (
        f"record={record} p={p} expected last_attack_landed={exp_last_attack}, got {got_last_attack}"
    )

    got_combo = int(out["combo_count"][0, p])
    exp_combo = int(row["ref_t1"]["combo_count"][0, p])
    assert got_combo == exp_combo, f"record={record} p={p} expected combo_count={exp_combo}, got {got_combo}"


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz",
            5355,
            0,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            1758,
            1,
        ),
    ],
)
def test_item_domain_last_attack_does_not_inherit_body_default_id_fallback(
    dataset_rel: str, record: int, p: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > record, f"replay too short for regression check: num_records={int(samples.shape[0])}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]

    # Replay-real guard for non-BODY bookkeeping:
    # - fighter attack_id is default (1),
    # - attacker still carries a prior concrete last_attack_landed,
    # - an owned item is present this frame,
    # - ref_t1 advances last_attack_landed in the item attack-id domain.
    assert int(seed["attack_id"][p]) == 1
    assert int(seed["last_attack_landed"][p]) != 0
    assert any(int(item["exists"]) and int(item["owner"]) == p for item in seed["items"])
    assert int(row["ref_t1"]["last_attack_landed"][0, p]) != int(seed["last_attack_landed"][p])

    out = _one_step_out_compare(ds=ds, row=row)
    got_last_attack = int(out["last_attack_landed"][0, p])
    exp_last_attack = int(row["ref_t1"]["last_attack_landed"][0, p])
    assert got_last_attack == exp_last_attack, (
        f"record={record} p={p} expected item-domain last_attack_landed={exp_last_attack}, "
        f"got {got_last_attack}"
    )


def test_same_frame_damage_keeps_residual_hitcapsule_source_identity_cnm_744() -> None:
    # Yoshi's Story simultaneous-hit lock:
    # - p1 Shine damages p0 early enough that p0 has already entered DamageHi1 before the BODY
    #   resolver processes p0's still-live AttackHi4 capsule.
    # - Source collision has already built that HitCapsule from frame-start fp->{x2068,x206C} and
    #   the frame-start attacker GObj; ftColl_80076ED8 must therefore attribute p1's damage to the
    #   pre-damage AttackHi4 source instead of p0's post-damage DamageHi1 instance.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_80076ED8}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    record = 744
    row = ds.rows[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    assert int(seed["action_id"][0]) == 63  # AttackHi4
    assert int(seed["action_id"][1]) == 345  # SpecialAirLwLoop
    assert int(ref["action_id"][0]) == 75  # DamageHi1
    assert int(ref["action_id"][1]) == 90  # DamageFlyTop

    out = _one_step_out_compare(ds=ds, row=row)[0]
    for p in (0, 1):
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert np.isclose(float(out["percent"][p]), float(ref["percent"][p]), rtol=0.0, atol=1e-6)
        assert int(out["instance_hit_by"][p]) == int(ref["instance_hit_by"][p])
        assert int(out["last_attack_landed"][p]) == int(ref["last_attack_landed"][p])


def test_body_damage_log_record_call_keeps_residual_exclusion_and_hit_group_order() -> None:
    # Regression guard for the delayed BODY damage log call site. `pre_combat_residual_hitcapsule_owner`
    # is the stale-instance exclusion flag passed into damage-product/staling calculation, while
    # `hit_group` is the group re-registered by `combat_body_damage_log_register_accepted_hitlists`.
    # Swapping them makes residual same-frame source hits register the accepted BODY hit under the
    # wrong group and excludes the wrong stale attack instance.
    src = (Path(__file__).resolve().parents[1] / "src/combat.c").read_text()
    call_start = src.index("combat_body_damage_log_record(\n", src.index("body_damage_logs[defender]"))
    call_end = src.index("rehit_frames);", call_start)
    call = " ".join(src[call_start:call_end].split())
    assert (
        "pre_combat_attack_instance[attacker], pre_combat_instance_id[attacker], "
        "pre_combat_residual_hitcapsule_owner[attacker], hit_group"
    ) in call
