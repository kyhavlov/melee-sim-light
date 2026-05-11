from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, seed_stride
        ).copy()
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        ).copy()
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        ref = row["ref_t1"][0]
        return out, ref
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_attackairlw_same_port_stale_owner_rehit_lands_on_replay_frame() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/"
        / "AttachedGoodNaturedGuanaco.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))

    # Replay-real timing lock for the AttackAirLw stale-owner bridge:
    # - Falco Dair keeps same-group hitboxes live across the active window while hitlist ownership
    #   is rewired on create/copy lanes in ftAction_8007121C / ftColl_800768A0.
    # - Teacher-forced reseed only carries dense victim presence + instance_hit_by, so same-port
    #   stale suppression must not fire a frame early on the continuing DamageFlyTop victim.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
    cases = [
        (7047, 1, 90, 0, 8, [192, 0, 0, 2, 0]),
        (7048, 1, 86, 7, 28, [192, 48, 0, 2, 0]),
        (7049, 1, 86, 6, 28, [192, 48, 0, 2, 0]),
    ]

    for record, p, exp_action, exp_hitlag, exp_hitstun, exp_flags in cases:
        assert int(ds.samples.shape[0]) > record, f"dataset too short for rec={record}"
        out, ref = _step_one_row(dataset_path, record)

        assert int(ref["action_id"][p]) == exp_action
        assert int(ref["hitlag"][p]) == exp_hitlag
        assert int(ref["hitstun"][p]) == exp_hitstun
        assert [int(x) for x in ref["state_flags"][p]] == exp_flags

        assert int(out["action_id"][p]) == exp_action
        assert int(out["hitlag"][p]) == exp_hitlag
        assert int(out["hitstun"][p]) == exp_hitstun
        assert [int(x) for x in out["state_flags"][p]] == exp_flags


@pytest.mark.integration
def test_attackairlw_damageflytop_fox_tail_pose_rejects_cdo_terminal_hit() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/"
        / "CornyDelayedOkapi.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 5241
    assert int(ds.samples.shape[0]) > record, f"dataset too short for rec={record}"
    row = ds.samples[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]

    # CDO:5241 is a terminal Fox DamageFlyTop frame under p1 AttackAirLw. The filtered BODY
    # candidate is Fox cap12 on FtPart 18; the dynamic tail-chain pose owner rejects that stale
    # tail contact instead of using a broad dense-hitlist latch that would also suppress valid Falco
    # DamageFlyTop contacts.
    assert int(seed["action_id"][1]) == 69  # AttackAirLw
    assert int(seed["action_id"][0]) == 90  # DamageFlyTop
    assert int(seed["hitstun"][0]) > 0
    assert int(seed["hitlag"][0]) == 0
    assert int(seed["combat_hitlist_cd"][1, 0, 0]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][1, 0, 0]) == int(seed["instance_id"][0])
    assert int(seed["last_hit_by"][0]) == 1
    assert int(seed["instance_hit_by"][0]) != int(seed["instance_id"][1])
    assert int(ref["action_id"][0]) == 90
    assert int(ref["hitlag"][0]) == 0
    assert int(ref["hitstun"][0]) == int(seed["hitstun"][0]) - 1

    out, ref = _step_one_row(dataset_path, record)
    for p in (0, 1):
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
        assert int(out["instance_hit_by"][p]) == int(ref["instance_hit_by"][p])
        assert int(out["last_attack_landed"][p]) == int(ref["last_attack_landed"][p])


@pytest.mark.integration
def test_attackairlw_damageflytop_falco_tail_neighbor_still_hits_hvg() -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl"
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    record = 9277
    assert int(ds.samples.shape[0]) > record, f"dataset too short for rec={record}"
    row = ds.samples[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]
    attacker = 1
    defender = 0

    assert int(seed["char_id"][defender]) == 22  # Falco, not Fox part-18 tail chain.
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw
    assert int(seed["action_id"][defender]) == 90  # DamageFlyTop
    assert int(seed["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][attacker, 0, defender]) == int(seed["instance_id"][defender])
    assert all(int(seed["combat_hitlist_hb_valid"][attacker, hb]) == 0 for hb in range(4))
    assert int(ref["action_id"][defender]) == 88  # DamageFlyLw from the fresh DAir BODY hit.
    assert int(ref["hitlag"][attacker]) > 0
    assert int(ref["hitlag"][defender]) > 0

    out, ref = _step_one_row(dataset_path, record)
    for p in (attacker, defender):
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
        assert int(out["instance_hit_by"][p]) == int(ref["instance_hit_by"][p])
        assert int(out["last_attack_landed"][p]) == int(ref["last_attack_landed"][p])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "attacker", "defender", "defender_action"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/"
            "BlondHardHippopotamus.msl",
            5412,
            0,
            1,
            76,  # DamageHi3
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
            "TreasuredBackKangaroo.msl",
            1848,
            0,
            1,
            76,  # DamageHi3
        ),
    ],
)
def test_attackairlw_authoritative_empty_hb_seed_allows_same_source_damage_followup(
    dataset_rel: str, record: int, attacker: int, defender: int, defender_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    assert int(ds.samples.shape[0]) > record, f"dataset too short for rec={record}"
    row = ds.samples[record]
    seed = row["seed_t"]
    ref = row["ref_t1"]

    # The dense group lane still names the defender from the previous same-source hit, but the
    # per-HitCapsule lane is authoritative-empty for the live DAir slots. That empty seed owns the
    # boundary and must not be backfilled from BODY attribution.
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw
    assert int(seed["action_id"][defender]) == defender_action
    assert int(seed["hitstun"][defender]) > 0
    assert int(seed["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][attacker, 0, defender]) == int(seed["instance_id"][defender])
    assert int(seed["last_hit_by"][defender]) == int(seed["source_port0"][attacker])
    assert any(int(seed["combat_hitlist_hb_valid"][attacker, hb]) != 0 for hb in range(4))
    for hb in range(4):
        if int(seed["combat_hitlist_hb_valid"][attacker, hb]) != 0:
            assert int(seed["combat_hitlist_hb_cd"][attacker, hb, defender]) == 0
    assert int(ref["hitlag"][attacker]) > 0
    assert int(ref["hitlag"][defender]) > 0

    out, ref = _step_one_row(dataset_path, record)
    for p in (attacker, defender):
        assert int(out["action_id"][p]) == int(ref["action_id"][p])
        assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
        assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
        assert int(out["combo_count"][p]) == int(ref["combo_count"][p])
        assert [int(x) for x in out["state_flags"][p]] == [int(x) for x in ref["state_flags"][p]]
