from __future__ import annotations

import importlib
from pathlib import Path
from dataclasses import dataclass

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _ThrowLwArticleCase:
    dataset_rel: str
    record: int
    attacker: int
    victim: int
    item_slot: int
    note: str


def _run_record(dataset_path: Path, record: int) -> np.ndarray:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    num_records = int(samples.shape[0])
    assert num_records > record, f"dataset too short for regression check: num_records={num_records}"

    row = samples[record : record + 1]

    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
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
@pytest.mark.parametrize("record", [448, 449, 8116, 8117])
def test_thrownlw_victim_hit_by_attached_laser_stays_thrownlw(record: int) -> None:
    # Locks in the QuerulousGrandDinosaur.msl offenders:
    # - seed_t: p0=ThrowLw (0xDE), p1=ThrownLw (0xF2), grab_owner_port[p1]=0 (attached)
    # - A Falco blaster laser overlaps the victim while attached.
    # - ref_t1: victim remains ThrownLw with hitstun==0 (no forced Damage* entry), and the laser persists.
    root = Path(__file__).resolve().parents[1]
    expected_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / expected_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {expected_rel}")

    p_attacker = 0
    p_victim = 1
    laser_slot = 1

    ds = read_dataset(str(dataset_path))
    row = ds.samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p_attacker]) == 0x00DE  # ThrowLw
    assert int(row["seed_t"]["action_id"][0, 1]) == 0x00F2  # ThrownLw
    assert int(row["seed_t"]["grab_owner_port"][0, 1]) == 0
    assert int(row["seed_t"]["items"][0, laser_slot]["exists"]) == 1
    assert int(row["seed_t"]["items"][0, laser_slot]["owner"]) == p_attacker

    expected_action = int(row["ref_t1"]["action_id"][0, p_victim])
    expected_item = row["ref_t1"]["items"][0, laser_slot]
    expected_laser_exists = int(expected_item["exists"])
    expected_laser_type = int(expected_item["type"])
    expected_laser_owner = int(expected_item["owner"])
    expected_laser_iid = int(expected_item["instance_id"])

    assert expected_action == 0x00F2
    assert expected_laser_exists == 1

    out = _run_record(dataset_path, record)
    got_action = int(out["action_id"][0, p_victim])
    got_item = out["items"][0, laser_slot]
    got_laser_exists = int(got_item["exists"])
    got_laser_type = int(got_item["type"])
    got_laser_owner = int(got_item["owner"])
    got_laser_iid = int(got_item["instance_id"])

    assert got_action == expected_action

    # Bookkeeping parity for the "suppressed attached-victim hit":
    # - victim stays in ThrownLw (no Damage* entry),
    # - victim percent/hitlag can still update,
    # - attacker/victim per-frame bookkeeping fields match ref_t1 exactly.
    for p in (p_attacker, p_victim):
        exp_hitlag = int(row["ref_t1"]["hitlag"][0, p])
        exp_hitstun = int(row["ref_t1"]["hitstun"][0, p])
        exp_percent = np.float32(row["ref_t1"]["percent"][0, p])
        exp_last_hit_by = int(row["ref_t1"]["last_hit_by"][0, p])
        exp_instance_hit_by = int(row["ref_t1"]["instance_hit_by"][0, p])
        exp_last_attack_landed = int(row["ref_t1"]["last_attack_landed"][0, p])
        exp_combo_count = int(row["ref_t1"]["combo_count"][0, p])

        got_hitlag = int(out["hitlag"][0, p])
        got_hitstun = int(out["hitstun"][0, p])
        got_percent = np.float32(out["percent"][0, p])
        got_last_hit_by = int(out["last_hit_by"][0, p])
        got_instance_hit_by = int(out["instance_hit_by"][0, p])
        got_last_attack_landed = int(out["last_attack_landed"][0, p])
        got_combo_count = int(out["combo_count"][0, p])

        assert got_hitlag == exp_hitlag
        assert got_hitstun == exp_hitstun
        assert got_last_hit_by == exp_last_hit_by
        assert got_instance_hit_by == exp_instance_hit_by
        assert got_last_attack_landed == exp_last_attack_landed
        assert got_combo_count == exp_combo_count

        exp_bits = np.frombuffer(exp_percent.tobytes(), dtype=np.uint32)[0]
        got_bits = np.frombuffer(got_percent.tobytes(), dtype=np.uint32)[0]
        assert got_bits == exp_bits

    assert got_laser_exists == expected_laser_exists
    assert got_laser_type == expected_laser_type
    assert got_laser_owner == expected_laser_owner
    assert got_laser_iid == expected_laser_iid


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ThrowLwArticleCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            record=9182,
            attacker=0,
            victim=1,
            item_slot=1,
            note="Fox ThrowLw frame-28 command refreshes expiring attached state1 article",
        ),
        _ThrowLwArticleCase(
            dataset_rel="datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl",
            record=9185,
            attacker=0,
            victim=1,
            item_slot=1,
            note="Fox ThrowLw frame-31 command refreshes expiring attached state1 article",
        ),
    ],
)
def test_throwlw_late_attached_replacement_article_locks(case: _ThrowLwArticleCase) -> None:
    # Replay-real locks for the late attached ThrowLw replacement-spawn owner:
    # - ftAction/ftFx_Throw_Anim emit a later set_throw_spawn_projectile pulse while the attached
    #   victim is still in ThrownLw.
    # - The seed-visible state1 article is at lifetime 1; vanilla refreshes the throw-side state1
    #   article and carries it through item serialization with the attached victim in hb0/1
    #   victims_1 state.
    # refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    # refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4}
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {case.dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples[case.record]["seed_t"]
    ref = ds.samples[case.record]["ref_t1"]
    assert int(seed["action_id"][case.attacker]) == 0x00DE, case.note  # ThrowLw
    assert int(seed["action_id"][case.victim]) == 0x00F2, case.note  # ThrownLw
    assert int(seed["grab_owner_port"][case.victim]) == case.attacker, case.note
    assert int(seed["throw_command_pending_pulse_frame"][case.attacker]) in (28, 31), case.note
    assert int(seed["items"][case.item_slot]["exists"]) == 1, case.note
    assert float(seed["items"][case.item_slot]["timer"]) <= 1.0, case.note
    assert int(ref["items"][case.item_slot]["exists"]) == 1, case.note

    out = _run_record(dataset_path, case.record)
    for field in ("exists", "type", "state", "owner", "instance_id"):
        got = int(out["items"][0, case.item_slot][field])
        exp = int(ref["items"][case.item_slot][field])
        assert got == exp, f"{case.note}: item_{field} expected={exp} got={got}"
