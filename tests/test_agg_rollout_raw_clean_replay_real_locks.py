from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


AGG = (
    "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
    "AttachedGoodNaturedGuanaco.msl"
)

ACT_ATTACK_AIR_LW = 0x45
ACT_ATTACK_LW4 = 0x40
ACT_DAMAGE_AIR_3 = 0x56
ACT_DAMAGE_FLY_HI = 0x57
ACT_DAMAGE_FLY_N = 0x58
ACT_DAMAGE_FLY_TOP = 0x5A
ACT_GUARD_SET_OFF = 0xB5
ACT_WAIT = 0x14


def _collision_contact_dtype() -> np.dtype:
    return np.dtype(
        [
            ("wall_kind", ("u1", (4,))),
            ("_pad0", ("u1", (4,))),
            ("wall_id", ("<u2", (4,))),
            ("wall_contact_x", ("<f4", (4,))),
            ("wall_contact_y", ("<f4", (4,))),
            ("wall_normal_x", ("<f4", (4,))),
            ("wall_normal_y", ("<f4", (4,))),
            ("ceiling_id", ("<u2", (4,))),
            ("_pad1", ("<u2", (4,))),
            ("ceiling_contact_x", ("<f4", (4,))),
            ("ceiling_contact_y", ("<f4", (4,))),
            ("ceiling_normal_x", ("<f4", (4,))),
            ("ceiling_normal_y", ("<f4", (4,))),
            ("coll_env_flags", ("<u4", (4,))),
            ("coll_prev_env_flags", ("<u4", (4,))),
            ("damage_hitlag_wall_asdi_latch", ("u1", (4,))),
        ],
        align=False,
    )


def _dataset_path(root: Path) -> Path:
    dataset_path = root / AGG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {AGG}")
    return dataset_path


def _field_bytes(samples: np.ndarray, record: int, field: str, stride: int) -> np.ndarray:
    row = samples[record : record + 1]
    return np.frombuffer(row[field].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)


def _run_rollout_records(
    dataset_path: Path, start_record: int, target_records: tuple[int, ...]
) -> dict[int, tuple[np.void, np.void, np.void]]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > max(target_records)

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contact_dtype = _collision_contact_dtype()

    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    contact_bytes = np.zeros((1, contact_dtype.itemsize), dtype=np.uint8)
    out_by_record: dict[int, tuple[np.void, np.void, np.void]] = {}

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(
            handle, _field_bytes(samples, start_record, "seed_t", seed_stride)
        )
        for record in range(start_record, max(target_records) + 1):
            binding.step_input(
                handle,
                _field_bytes(samples, record, "prev_input_t", input_stride),
                _field_bytes(samples, record, "input_t", input_stride),
            )
            if record in target_records:
                binding.write_compare(handle, out_compare_bytes)
                binding.debug_write_collision_contacts(handle, contact_bytes)
                out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
                contacts = contact_bytes.view(contact_dtype).reshape(-1)[0].copy()
                out_by_record[record] = (
                    samples[record]["ref_t1"].copy(),
                    out,
                    contacts,
                )
    finally:
        binding.destroy(handle)

    return out_by_record


@pytest.mark.integration
def test_agg_attackairlw_tail_chain_suppresses_then_releases_damageflytop_body() -> None:
    # AGG rec 7047/7048 rollout lock:
    # - Falco AttackAirLw hb1 overlaps Fox's dynamic tail-chain BODY cap while the victim is in
    #   active DamageFlyTop.
    # - Source dynamic part-18 ownership suppresses the earlier terminal tail BODY candidate, then
    #   releases at the next same-source hitlag horizon.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)

    by_record = _run_rollout_records(dataset_path, 7045, (7047, 7048))

    ref_7047, out_7047, contacts_7047 = by_record[7047]
    assert int(out_7047["action_id"][0]) == int(ref_7047["action_id"][0]) == ACT_ATTACK_AIR_LW
    assert int(out_7047["action_id"][1]) == int(ref_7047["action_id"][1]) == ACT_DAMAGE_FLY_TOP
    assert int(out_7047["hitlag"][1]) == int(ref_7047["hitlag"][1]) == 0
    assert int(out_7047["hitstun"][1]) == int(ref_7047["hitstun"][1]) == 8
    assert int(contacts_7047["damage_hitlag_wall_asdi_latch"][1]) == 0
    np.testing.assert_allclose(out_7047["pos_x"][:2], ref_7047["pos_x"][:2], atol=1e-6)

    ref_7048, out_7048, contacts_7048 = by_record[7048]
    assert int(out_7048["action_id"][1]) == int(ref_7048["action_id"][1]) == ACT_DAMAGE_AIR_3
    assert int(out_7048["hitlag"][0]) == int(ref_7048["hitlag"][0]) == 7
    assert int(out_7048["hitlag"][1]) == int(ref_7048["hitlag"][1]) == 7
    assert int(out_7048["hitstun"][1]) == int(ref_7048["hitstun"][1]) == 28
    assert int(contacts_7048["damage_hitlag_wall_asdi_latch"][1]) == 0
    np.testing.assert_allclose(out_7048["pos_x"][:2], ref_7048["pos_x"][:2], atol=1e-6)


@pytest.mark.integration
def test_agg_damagefly_hitlag_exit_ignores_stale_right_wall_asdi_latch() -> None:
    # AGG rec 7163/7164/7263 rollout lock:
    # - AttackLw4 enters DamageFlyN near a stale right-wall CollData id.
    # - DamageFly_Coll must not inherit generic persisted-wall projection when the source
    #   DamageFly right-wall envelope is not active; otherwise Damage_OnExitHitlag projects ASDI
    #   away and the later AttackAirLw hit at rec 7263 misses.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800454A4_RightWall}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)

    by_record = _run_rollout_records(dataset_path, 7156, (7163, 7164, 7263))

    ref_7163, out_7163, contacts_7163 = by_record[7163]
    assert int(out_7163["action_id"][0]) == int(ref_7163["action_id"][0]) == ACT_ATTACK_LW4
    assert int(out_7163["action_id"][1]) == int(ref_7163["action_id"][1]) == ACT_DAMAGE_FLY_N
    assert int(out_7163["hitlag"][1]) == int(ref_7163["hitlag"][1]) == 1
    assert int(contacts_7163["coll_env_flags"][1]) == 0
    assert int(contacts_7163["damage_hitlag_wall_asdi_latch"][1]) == 0

    ref_7164, out_7164, contacts_7164 = by_record[7164]
    assert int(out_7164["action_id"][1]) == int(ref_7164["action_id"][1]) == ACT_DAMAGE_FLY_N
    assert int(out_7164["hitlag"][1]) == int(ref_7164["hitlag"][1]) == 0
    assert int(out_7164["hitstun"][1]) == int(ref_7164["hitstun"][1]) == 39
    assert int(contacts_7164["damage_hitlag_wall_asdi_latch"][1]) == 0
    np.testing.assert_allclose(out_7164["pos_x"][:2], ref_7164["pos_x"][:2], atol=1e-6)

    ref_7263, out_7263, contacts_7263 = by_record[7263]
    assert int(out_7263["action_id"][0]) == int(ref_7263["action_id"][0]) == ACT_ATTACK_AIR_LW
    assert int(out_7263["action_id"][1]) == int(ref_7263["action_id"][1]) == ACT_DAMAGE_FLY_HI
    assert int(out_7263["hitlag"][0]) == int(ref_7263["hitlag"][0]) == 5
    assert int(out_7263["hitlag"][1]) == int(ref_7263["hitlag"][1]) == 5
    assert int(out_7263["hitstun"][1]) == int(ref_7263["hitstun"][1]) == 41
    assert int(contacts_7263["damage_hitlag_wall_asdi_latch"][1]) == 0
    np.testing.assert_allclose(out_7263["pos_x"][:2], ref_7263["pos_x"][:2], atol=1e-4)


@pytest.mark.integration
def test_agg_powershield_dair_x19a4_recoil_keeps_later_ledge_wait() -> None:
    # AGG rec 2390 -> 2491 rollout lock:
    # - Fox AttackAirLw's low-damage DAir capsule hits an x221C_b2 GuardReflect shield.
    # - ftColl_80076CBC writes x19A4/x19AC before the powershield branch, and
    #   ftCo_80092F2C consumes that authored current HitCapsule damage for GuardSetOff recoil.
    # - If x19A4 is re-staled through the live stale queue, p0 is 0.09 units/frame short during
    #   GuardSetOff, p1 receives an extra grounded nudge later, and rec 2491 falls one frame early.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_8007ABD0}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    # data/moves/fox.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = _dataset_path(root)

    by_record = _run_rollout_records(dataset_path, 2368, (2390, 2491))

    ref_2390, out_2390, _contacts_2390 = by_record[2390]
    assert int(out_2390["action_id"][0]) == int(ref_2390["action_id"][0]) == ACT_GUARD_SET_OFF
    assert int(out_2390["hitlag"][0]) == int(ref_2390["hitlag"][0]) == 3
    assert float(out_2390["shield_hp"][0]) == pytest.approx(float(ref_2390["shield_hp"][0]))
    assert float(out_2390["speed_ground_x_self"][0]) == pytest.approx(
        float(ref_2390["speed_ground_x_self"][0]), abs=1e-6
    )

    ref_2491, out_2491, _contacts_2491 = by_record[2491]
    assert int(out_2491["action_id"][1]) == int(ref_2491["action_id"][1]) == ACT_WAIT
    np.testing.assert_allclose(out_2491["pos_x"][:2], ref_2491["pos_x"][:2], atol=1e-5)
