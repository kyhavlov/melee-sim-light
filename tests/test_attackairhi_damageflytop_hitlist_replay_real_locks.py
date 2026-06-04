from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_ATTACK_AIR_HI = 0x0044
ACT_DAMAGE_AIR_2 = 0x0055
ACT_DAMAGE_FLY_TOP = 0x005A


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _dataset_path(root: Path) -> Path:
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")
    return dataset_path


def _step_row_with_seed(dataset_path: Path, record: int, seed: np.ndarray) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(), row["ref_t1"][0].copy()


def _run_rollout_records(
    dataset_path: Path, start_record: int, records: tuple[int, ...]
) -> dict[int, tuple[np.void, np.void]]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    def _bytes(row_field: np.ndarray, stride: int) -> np.ndarray:
        return np.frombuffer(row_field.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    got: dict[int, tuple[np.void, np.void]] = {}
    try:
        binding.reseed_seed_rollout(
            handle, _bytes(ds.samples[start_record : start_record + 1]["seed_t"], seed_stride)
        )
        for record in range(start_record, max(records) + 1):
            row = ds.samples[record : record + 1]
            binding.step_input(
                handle,
                _bytes(row["prev_input_t"], input_stride),
                _bytes(row["input_t"], input_stride),
            )
            if record in records:
                binding.write_compare(handle, out_bytes)
                got[record] = (
                    out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(),
                    row["ref_t1"][0].copy(),
                )
    finally:
        binding.destroy(handle)
    return got


@pytest.mark.integration
def test_attackairhi_create_frame_keeps_same_source_damageflytop_dense_latch_tbk_5247() -> None:
    # Falco UpAir create-frame vs terminal Fox DamageFlyTop:
    # - TBK:5247 carries a dense victims_1 seed for p1's group-0 UAir hitbox and p0's current iid.
    # - BODY attribution still names the previous same-port DamageFlyTop source instance, but
    #   lbColl_8000ACFC owns suppression by HitCapsule victim presence, not instance_hit_by.
    # - The next row has authoritative per-hitbox empty seeds and must allow the real hit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)
    ds = read_dataset(str(dataset_path))

    attacker = 1
    victim = 0
    seed = ds.samples[5247:5248]["seed_t"].copy()
    assert int(seed[0]["action_id"][attacker]) == ACT_ATTACK_AIR_HI
    assert int(seed[0]["action_frame"][attacker]) == 7
    assert int(seed[0]["action_id"][victim]) == ACT_DAMAGE_FLY_TOP
    assert int(seed[0]["hitstun"][victim]) == 3
    assert int(seed[0]["last_hit_by"][victim]) == int(seed[0]["source_port0"][attacker])
    assert int(seed[0]["instance_hit_by"][victim]) != int(seed[0]["instance_id"][attacker])
    assert int(seed[0]["combat_hitlist_cd"][attacker, 0, victim]) == 0xFFFF
    assert int(seed[0]["combat_hitlist_victim_iid"][attacker, 0, victim]) == int(
        seed[0]["instance_id"][victim]
    )

    out, ref = _step_row_with_seed(dataset_path, 5247, seed)
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == ACT_DAMAGE_FLY_TOP
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]) == 0
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]), abs=1e-6)

    poisoned = seed.copy()
    poisoned[0]["combat_hitlist_cd"][attacker, 0, victim] = np.uint16(0)
    poisoned[0]["combat_hitlist_victim_iid"][attacker, 0, victim] = np.uint16(0)
    out_without_latch, _ = _step_row_with_seed(dataset_path, 5247, poisoned)
    assert int(out_without_latch["action_id"][victim]) == ACT_DAMAGE_AIR_2
    assert int(out_without_latch["hitlag"][victim]) > 0
    assert float(out_without_latch["percent"][victim]) > float(ref["percent"][victim])


@pytest.mark.integration
def test_attackairhi_next_row_authoritative_empty_hitcapsule_allows_tbk_5248_hit() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)
    ds = read_dataset(str(dataset_path))

    attacker = 1
    victim = 0
    seed = ds.samples[5248:5249]["seed_t"].copy()
    assert int(seed[0]["action_id"][attacker]) == ACT_ATTACK_AIR_HI
    assert int(seed[0]["action_id"][victim]) == ACT_DAMAGE_FLY_TOP
    assert int(seed[0]["combat_hitlist_cd"][attacker, 0, victim]) == 0xFFFF
    assert [int(v) for v in seed[0]["combat_hitlist_hb_valid"][attacker, :3]] == [1, 1, 1]
    assert [int(seed[0]["combat_hitlist_hb_cd"][attacker, hb, victim]) for hb in range(3)] == [
        0,
        0,
        0,
    ]

    out, ref = _step_row_with_seed(dataset_path, 5248, seed)
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim]) == ACT_DAMAGE_AIR_2
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]) == 5
    assert int(out["hitstun"][victim]) == int(ref["hitstun"][victim]) == 16
    assert float(out["percent"][victim]) == pytest.approx(float(ref["percent"][victim]), abs=1e-6)


@pytest.mark.integration
def test_attackairhi_rollout_create_edge_suppresses_then_releases_tbk_5247() -> None:
    # Runtime rollout lock for the no-dense-seed side of the same source owner:
    # - The rollout starts before Falco's UpAir create edge, so rec 5247 has no row-local dense
    #   group seed to materialize. BODY attribution still proves Fox is in the same terminal
    #   DamageFlyTop source episode, and vanilla suppresses the create-frame BODY fallthrough.
    # - The next source frame is not allowed to inherit that suppression into the newly-created
    #   HitCapsule; rec 5248 admits the real UpAir BODY hit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8,ftColl_80078C70}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)
    ds = read_dataset(str(dataset_path))

    attacker = 1
    victim = 0
    start_record = 5124
    rows = _run_rollout_records(dataset_path, start_record, (5247, 5248))
    out_5247, ref_5247 = rows[5247]
    out_5248, ref_5248 = rows[5248]

    seed_5247 = ds.samples[5247]["seed_t"]
    assert int(seed_5247["action_id"][attacker]) == ACT_ATTACK_AIR_HI
    assert int(seed_5247["action_id"][victim]) == ACT_DAMAGE_FLY_TOP
    assert int(seed_5247["hitstun"][victim]) == 3
    assert int(seed_5247["instance_hit_by"][victim]) != int(seed_5247["instance_id"][attacker])
    assert int(seed_5247["last_hit_by"][victim]) == int(seed_5247["source_port0"][attacker])

    assert int(out_5247["action_id"][victim]) == int(ref_5247["action_id"][victim]) == ACT_DAMAGE_FLY_TOP
    assert int(out_5247["hitlag"][victim]) == int(ref_5247["hitlag"][victim]) == 0
    assert float(out_5247["percent"][victim]) == pytest.approx(float(ref_5247["percent"][victim]), abs=1e-6)

    assert int(out_5248["action_id"][victim]) == int(ref_5248["action_id"][victim]) == ACT_DAMAGE_AIR_2
    assert int(out_5248["hitlag"][victim]) == int(ref_5248["hitlag"][victim]) == 5
    assert float(out_5248["percent"][victim]) == pytest.approx(float(ref_5248["percent"][victim]), abs=1e-6)
