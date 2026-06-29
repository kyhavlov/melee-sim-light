"""Sheik special state-machine locks.

These tests cover source-owned motion-state dispatch and callback skeletons without using replay
rows as targets. Article/projectile fidelity for Needles/Chain remains item-system work.

Sources:
- refs/melee/src/melee/ft/chara/ftSeak/forward.h
- refs/melee/src/melee/ft/chara/ftSeak/ftSk_Special{N,S,Hi,Lw}.c
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))

from test_char_common_action_coverage import ACT_GUARD, _mk_inputs, _run, _seed_base  # noqa: E402

ACT_GUARD_REFLECT = 0x00B6
from test_colldata_ecb_substrate import _colldata_ecb_dtype  # noqa: E402
from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, read_dataset  # noqa: E402
from tools.slippi.item_article_data import item_article_values_by_sim_char  # noqa: E402

pytest.importorskip("msl_binding")

A = 0x0100
B = 0x0200

ACT_WAIT = 0x000E
ACT_WALK_SLOW = 0x000F
ACT_TURN = 0x0012
ACT_DASH = 0x0014
ACT_RUN = 0x0015
ACT_RUN_BRAKE = 0x0017
ACT_FALL = 0x001D
ACT_FALL_SPECIAL = 0x0023
ACT_KNEE_BEND = 0x0018
ACT_SQUAT = 0x0027
ACT_JUMP_F = 0x0019
ACT_LANDING = 0x002A
ACT_LANDING_FALL_SPECIAL = 0x002B
ACT_ATTACK_AIR_N = 0x0041
ACT_ATTACK_AIR_B = 0x0043
ACT_ATTACK_AIR_LW = 0x0045
ACT_ATTACK_100_START = 0x002F
ACT_LANDING_AIR_N = 0x0046
ACT_DAMAGE_N_1 = 0x004E
ACT_DAMAGE_N_3 = 0x0050
ACT_DAMAGE_LW_2 = 0x0052
ACT_GUARD_ON = 0x00B2
ACT_FX_SPECIAL_N_START = 0x0155
ACT_FX_SPECIAL_HI_HOLD_AIR = 0x0162
ACT_SK_SPECIAL_N_START = 341
ACT_SK_SPECIAL_N_LOOP = 342
ACT_SK_SPECIAL_N_END = 344
ACT_SK_SPECIAL_AIR_N_START = 345
ACT_SK_SPECIAL_AIR_N_LOOP = 346
ACT_SK_SPECIAL_N_CANCEL = 343
ACT_SK_SPECIAL_AIR_N_CANCEL = 347
ACT_SK_SPECIAL_AIR_N_END = 348
ACT_SK_SPECIAL_S_START = 349
ACT_SK_SPECIAL_S = 350
ACT_SK_SPECIAL_S_END = 351
ACT_SK_SPECIAL_AIR_S_START = 352
ACT_SK_SPECIAL_AIR_S = 353
ACT_SK_SPECIAL_AIR_S_END = 354
ACT_SK_SPECIAL_HI_START_0 = 355
ACT_SK_SPECIAL_HI_START_1 = 356
ACT_SK_SPECIAL_HI = 357
ACT_SK_SPECIAL_AIR_HI_START_0 = 358
ACT_SK_SPECIAL_AIR_HI_START_1 = 359
ACT_SK_SPECIAL_AIR_HI = 360
ACT_SK_SPECIAL_LW = 361
ACT_SK_SPECIAL_LW_2 = 362
ACT_SK_SPECIAL_AIR_LW = 363
ACT_SK_SPECIAL_AIR_LW_2 = 364
ACT_ZD_SPECIAL_LW = 355
ACT_ZD_SPECIAL_LW_2 = 356
ACT_ZD_SPECIAL_AIR_LW = 357
ACT_ZD_SPECIAL_AIR_LW_2 = 358
ACT_CLIFF_CATCH = 252
CHAR_SHEIK = 7
CHAR_ZELDA = 19

SM_LANDING = 35
SM_LANDING_AIR_N = 73
HIT_GROUNDED = 1 << 9
HIT_AERIAL = 1 << 10

def _sheik_article_int(field_name: str) -> int:
    return int(item_article_values_by_sim_char(ROOT / "data", field_name)[7])


ITEM_SHEIK_NEEDLE_THROWN = _sheik_article_int("needle_throw_itkind")
ITEM_SHEIK_NEEDLE_HELD = _sheik_article_int("needle_held_itkind")
ITEM_SHEIK_VANISH = _sheik_article_int("vanish_itkind")
ITEM_SHEIK_CHAIN = _sheik_article_int("chain_itkind")


def test_sheik_special_attrs_are_required_for_runtime_load(tmp_path: Path) -> None:
    """Missing Sheik ftSeakAttributes keys must fail init instead of zeroing mechanics."""

    data_dir = tmp_path / "data"
    data_dir.mkdir()
    root_data = ROOT / "data"
    for child in root_data.iterdir():
        if child.name == "characters":
            continue
        (data_dir / child.name).symlink_to(child, target_is_directory=child.is_dir())

    chars_dir = data_dir / "characters"
    chars_dir.mkdir()
    for src in (root_data / "characters").glob("*.json"):
        dst = chars_dir / src.name
        if src.name != "sheik.json":
            dst.write_bytes(src.read_bytes())
            continue
        attrs = json.loads(src.read_text(encoding="utf-8"))
        attrs.pop("sheik_vanish_travel_frames")
        dst.write_text(json.dumps(attrs, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    code = "import msl_binding\nmsl_binding.init(batch_size=1, num_players=2)\n"
    env = os.environ.copy()
    env["MSL_DATA_DIR"] = str(data_dir)
    proc = subprocess.run([sys.executable, "-c", code], env=env, text=True, capture_output=True)
    assert proc.returncode != 0
    combined = proc.stderr + proc.stdout
    assert "char params Sheik special attr parse failed" in combined
    assert "msl_batch_create failed" in combined


def _run_sample_row(
    samples: np.ndarray,
    record: int,
    *,
    mutate_input_y: int | None = None,
    mutate_seed: dict[str, int | tuple[int, int]] | None = None,
    mutate_item0_type: int | None = None,
    replay_frame_rng: bool = False,
) -> np.void:
    import msl_binding

    row = samples[record : record + 1].copy()
    if mutate_input_y is not None:
        row["input_t"]["p"]["main_y"][0, 0] = np.int8(mutate_input_y)
    if mutate_seed is not None:
        for field, value in mutate_seed.items():
            if isinstance(value, tuple):
                slot, raw = value
            else:
                slot, raw = 0, value
            row["seed_t"][field][0, slot] = raw
    if mutate_item0_type is not None:
        row["seed_t"]["items"]["type"][0, 0] = np.uint16(mutate_item0_type)

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = row["seed_t"].view(np.uint8).reshape((1, seed_stride)).copy()
    prev_input_bytes = row["prev_input_t"].view(np.uint8).reshape((1, input_stride)).copy()
    input_bytes = row["input_t"].view(np.uint8).reshape((1, input_stride)).copy()
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        if replay_frame_rng:
            msl_binding.step_input_replay_frame_rng(handle, seed_bytes, prev_input_bytes, input_bytes)
        else:
            msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def _run_sample_rollout_records(
    samples: np.ndarray,
    *,
    start_record: int,
    records: tuple[int, ...],
    replay_frame_rng: bool = False,
) -> dict[int, np.void]:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    out: dict[int, np.void] = {}
    wanted = set(records)
    max_record = max(wanted)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = (
            samples[start_record : start_record + 1]["seed_t"]
            .view(np.uint8)
            .reshape((1, seed_stride))
            .copy()
        )
        msl_binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, max_record + 1):
            row = samples[record : record + 1]
            prev_input_bytes = row["prev_input_t"].view(np.uint8).reshape((1, input_stride)).copy()
            input_bytes = row["input_t"].view(np.uint8).reshape((1, input_stride)).copy()
            if replay_frame_rng:
                frame_seed_bytes = row["seed_t"].view(np.uint8).reshape((1, seed_stride)).copy()
                msl_binding.step_input_replay_frame_rng(
                    handle, frame_seed_bytes, prev_input_bytes, input_bytes
                )
            else:
                msl_binding.step_input(handle, prev_input_bytes, input_bytes)
            if record in wanted:
                msl_binding.write_compare(handle, out_bytes)
                out[record] = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)

    return out


def _collision_contacts_dtype() -> np.dtype:
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


def _run_sample_row_with_contacts(samples: np.ndarray, record: int) -> tuple[np.void, np.void]:
    import msl_binding

    row = samples[record : record + 1].copy()
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contacts_stride = int(sizes["collision_contacts"])
    contacts_dtype = _collision_contacts_dtype()
    assert contacts_stride == contacts_dtype.itemsize

    seed_bytes = row["seed_t"].view(np.uint8).reshape((1, seed_stride)).copy()
    prev_input_bytes = row["prev_input_t"].view(np.uint8).reshape((1, input_stride)).copy()
    input_bytes = row["input_t"].view(np.uint8).reshape((1, input_stride)).copy()
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    contact_bytes = np.zeros((1, contacts_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_bytes)
        msl_binding.debug_write_collision_contacts(handle, contact_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        contacts = contact_bytes.view(contacts_dtype).reshape((1,))[0].copy()
        return out, contacts
    finally:
        msl_binding.destroy(handle)


def _run_sample_row_with_colldata(
    samples: np.ndarray,
    record: int,
    *,
    mutate_seed: dict[str, int | tuple[int, int]] | None = None,
) -> tuple[np.void, np.void]:
    import msl_binding

    row = samples[record : record + 1].copy()
    if mutate_seed is not None:
        for field, value in mutate_seed.items():
            if isinstance(value, tuple):
                slot, raw = value
            else:
                slot, raw = 0, value
            row["seed_t"][field][0, slot] = raw

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    colldata_stride = int(sizes["colldata_ecb"])
    colldata_dtype = _colldata_ecb_dtype()
    assert colldata_stride == colldata_dtype.itemsize

    seed_bytes = row["seed_t"].view(np.uint8).reshape((1, seed_stride)).copy()
    prev_input_bytes = row["prev_input_t"].view(np.uint8).reshape((1, input_stride)).copy()
    input_bytes = row["input_t"].view(np.uint8).reshape((1, input_stride)).copy()
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    colldata_bytes = np.zeros((1, colldata_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_bytes)
        msl_binding.debug_write_colldata_ecb(handle, colldata_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
        colldata = colldata_bytes.view(colldata_dtype).reshape((1,))[0].copy()
        return out, colldata
    finally:
        msl_binding.destroy(handle)


def _run_seed_one_step(seed: np.ndarray, prev: np.ndarray, cur: np.ndarray) -> np.void:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.step_input(handle, prev, cur)
        msl_binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


def _sheik_validation_samples(rel: str) -> np.ndarray:
    path = ROOT / rel
    if not path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    return read_dataset(str(path)).samples


def _item_type_present(row: np.void, item_type: int) -> bool:
    return any(
        int(t) == item_type and int(e) != 0
        for t, e in zip(row["items"]["type"], row["items"]["exists"])
    )


def _item_slot_for_type(row: np.void, item_type: int) -> int:
    for i, (t, e) in enumerate(zip(row["items"]["type"], row["items"]["exists"])):
        if int(t) == item_type and int(e) != 0:
            return i
    raise AssertionError(f"missing item type {item_type}")


def _attrs() -> dict:
    return json.loads((ROOT / "data" / "characters" / "sheik.json").read_text())


def test_sheik_grounded_b_special_dispatch_order_is_source_ordered() -> None:
    # Grounded Side-B owns the explicit x-stick check. Up/neutral/down are hidden-latch
    # callsites in ftCo_Wait_IASA/ftCo_Attack100; this synthetic entry test locks the
    # current live B-edge routing until those lanes are promoted.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D6824,ftCo_800D68C0}
    cases = [
        ("side", {"buttons": B, "main_x": 80, "main_y": 80}, ACT_SK_SPECIAL_S_START),
        ("up", {"buttons": B, "main_y": 80}, ACT_SK_SPECIAL_HI_START_0),
        ("down", {"buttons": B, "main_y": -80}, ACT_SK_SPECIAL_LW),
        ("neutral", {"buttons": B}, ACT_SK_SPECIAL_N_START),
    ]
    for name, axes, expected in cases:
        out = _run(_seed_base("sheik"), [_mk_inputs(**axes)])[0]
        assert int(out["action_id"][0]) == expected, name


def test_sheik_air_up_b_entry_clears_fastfall_state_flag() -> None:
    # ftSk_SpecialAirHi_Enter enters Start_0 through Fighter_ChangeMotionState without
    # Ft_MF_KeepFastFall, so a fastfalling Fall seed must clear fp->fall_fast / state_flags[1] 0x08
    # on the Vanish entry row. An adjacent Fall-without-Up-B frame keeps the fastfall lane.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHi_Enter
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    seed = _seed_base("sheik", grounded=False, pos_y=40.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(20)
    seed["fall_fast"][0, 0] = np.uint8(1)
    seed["state_flags"][0, 0, 1] = np.uint8(0x08)

    fall = _run(seed.copy(), [_mk_inputs()])[0]
    assert int(fall["action_id"][0]) == ACT_FALL
    assert int(fall["state_flags"][0][1]) & 0x08

    vanish = _run(seed, [_mk_inputs(buttons=B, main_y=80)])[0]
    assert int(vanish["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_0
    assert int(vanish["state_flags"][0][1]) & 0x08 == 0


@pytest.mark.integration
@pytest.mark.parametrize("record", [1259, 1786])
def test_sheik_kneebend_iasa_up_b_presence_beats_side_special_demo_lock(record: int) -> None:
    # KneeBend_IASA does not call the full grounded Wait B-special resolver. It calls
    # ftCo_Attack100_CheckInput first, and that source path admits only SpecialHi through x686==0.
    # These demo rows have B+diagonal-up during KneeBend and must enter Vanish, not Chain.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100_CheckInput
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_KNEE_BEND
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_HI_START_0

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_HI_START_0


def test_sheik_kneebend_iasa_does_not_admit_side_b_negative() -> None:
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_KNEE_BEND)
    seed["seed_prev_action_id"][0, 0] = np.uint16(ACT_KNEE_BEND)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)

    out = _run(seed, [_mk_inputs(buttons=B, main_x=80)])[0]
    assert int(out["action_id"][0]) != ACT_SK_SPECIAL_S_START
    assert int(out["action_id"][0]) == ACT_KNEE_BEND


@pytest.mark.integration
@pytest.mark.parametrize(
    ("replay", "record"),
    [
        ("datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl", 84),
        ("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl", 3979),
    ],
)
def test_sheik_landing_iasa_neutral_b_enters_needle_start_replay_real(
    replay: str, record: int
) -> None:
    # ftCo_Landing_IASA checks Sheik grounded specials after the normal landing-lag gate and
    # before grounded attacks/guard. These official rows are normal Landing, not LandingAir.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
    samples = _sheik_validation_samples(replay)
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][0]) == ACT_LANDING
    assert int(seed["char_id"][0]) == 7
    assert int(samples[record]["input_t"]["p"]["buttons"][0]) & B
    assert int(ref["action_id"][0]) == ACT_SK_SPECIAL_N_START

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_N_START


def test_sheik_landing_iasa_b_special_order_precedes_grounded_attack() -> None:
    # Source order: Landing special checks precede grounded attacks, so B+A on the first actionable
    # Landing frame still enters Sheik neutral special rather than Attack11.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
    lag = int(_attrs()["landing_lag_frames"])
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_LANDING)
    seed["animation_index"][0, 0] = np.uint32(SM_LANDING)
    seed["action_frame"][0, 0] = np.int16(lag)
    seed["anim_frame_f32"][0, 0] = np.float32(float(lag))

    out = _run_seed_one_step(seed, _mk_inputs(), _mk_inputs(buttons=A | B))
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_N_START


def test_sheik_landing_iasa_b_special_respects_lag_and_landingair_boundaries() -> None:
    # Adjacent negatives: pre-lag Landing is not interruptible, and LandingAir IASA is empty.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    #   ftCo_Landing_IASA,ftCo_LandingAir_IASA}
    lag = int(_attrs()["landing_lag_frames"])

    early = _seed_base("sheik")
    early["action_id"][0, 0] = np.uint16(ACT_LANDING)
    early["animation_index"][0, 0] = np.uint32(SM_LANDING)
    # The simulator advances animation before IASA, so seed two frames before the source landing
    # lag to remain below the gate when the Landing callback runs.
    pre_lag_seed_frame = max(0, lag - 2)
    early["action_frame"][0, 0] = np.int16(pre_lag_seed_frame)
    early["anim_frame_f32"][0, 0] = np.float32(float(pre_lag_seed_frame))
    early_out = _run_seed_one_step(early, _mk_inputs(), _mk_inputs(buttons=B))
    assert int(early_out["action_id"][0]) == ACT_LANDING

    landing_air = _seed_base("sheik")
    landing_air["action_id"][0, 0] = np.uint16(ACT_LANDING_AIR_N)
    landing_air["animation_index"][0, 0] = np.uint32(SM_LANDING_AIR_N)
    landing_air["action_frame"][0, 0] = np.int16(lag + 10)
    landing_air["anim_frame_f32"][0, 0] = np.float32(float(lag + 10))
    landing_air_out = _run_seed_one_step(landing_air, _mk_inputs(), _mk_inputs(buttons=B))
    assert int(landing_air_out["action_id"][0]) == ACT_LANDING_AIR_N


def test_sheik_air_b_special_dispatch_order_and_entry_velocity_are_source_owned() -> None:
    # ftCo_SpecialAir_CheckInput order is Up -> Down -> Side -> Neutral.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    cases = [
        ("up", {"buttons": B, "main_y": 80}, ACT_SK_SPECIAL_AIR_HI_START_0),
        ("down", {"buttons": B, "main_y": -80}, ACT_SK_SPECIAL_AIR_LW),
        ("side", {"buttons": B, "main_x": 80}, ACT_SK_SPECIAL_AIR_S_START),
        ("neutral", {"buttons": B}, ACT_SK_SPECIAL_AIR_N_START),
    ]
    for name, axes, expected in cases:
        seed = _seed_base("sheik", grounded=False, pos_y=30.0)
        seed["action_id"][0, 0] = np.uint16(ACT_FALL)
        seed["animation_index"][0, 0] = np.uint32(ACT_FALL)
        out = _run(seed, [_mk_inputs(**axes)])[0]
        assert int(out["action_id"][0]) == expected, name
        if name == "up":
            assert float(out["speed_y_self"][0]) > 0.0


def test_sheik_jump_iasa_can_enter_air_neutral_b_on_fresh_edge() -> None:
    # ftCo_Jump_IASA calls ftCo_SpecialAir_CheckInput before the non-special Jump IASA tail.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    seed = _seed_base("sheik", grounded=False, pos_y=30.0)
    seed["action_id"][0, 0] = np.uint16(ACT_JUMP_F)
    seed["animation_index"][0, 0] = np.uint32(ACT_JUMP_F)
    seed["action_frame"][0, 0] = np.int16(15)
    seed["anim_frame_f32"][0, 0] = np.float32(15.0)
    out = _run(seed, [_mk_inputs(buttons=B)])[0]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_N_START


def test_sheik_jump_iasa_requires_fresh_b_edge_for_air_neutral_b() -> None:
    import msl_binding

    seed = _seed_base("sheik", grounded=False, pos_y=30.0)
    seed["action_id"][0, 0] = np.uint16(ACT_JUMP_F)
    seed["animation_index"][0, 0] = np.uint32(ACT_JUMP_F)
    seed["action_frame"][0, 0] = np.int16(15)
    seed["anim_frame_f32"][0, 0] = np.float32(15.0)
    held_b = _mk_inputs(buttons=B)
    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        msl_binding.step_input(handle, held_b, held_b)
        out_bytes = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        msl_binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
        assert int(out["action_id"][0]) == ACT_JUMP_F
    finally:
        msl_binding.destroy(handle)


def test_sheik_action_id_overlap_does_not_enter_for_spacies() -> None:
    # Sheik's 341 action id equals Fox/Falco SpecialNStart numerically. The Sheik module must not
    # interpret that range for other characters.
    seed = _seed_base("fox")
    out = _run(seed, [_mk_inputs(buttons=B)])[0]
    assert int(out["action_id"][0]) == ACT_FX_SPECIAL_N_START
    assert int(out["action_id"][0]) != ACT_SK_SPECIAL_N_LOOP


def test_sheik_ground_vanish_start_down_stick_enters_ground_travel_from_source_attrs() -> None:
    # ftSk_SpecialHiStart_0_Anim runs from the Fighter callback phase, before the new frame's
    # input is promoted into fp->input. Lock the callback-visible prev-input lane.
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_HI_START_0)
    seed["animation_index"][0, 0] = np.uint32(309)
    seed["anim_frame_f32"][0, 0] = np.float32(200.0)
    out = _run_seed_one_step(seed, _mk_inputs(main_y=-80), _mk_inputs(main_y=-80))
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_HI_START_1
    assert int(out["animation_index"][0]) == 309
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-6)


def test_sheik_ground_vanish_launch_uses_callback_visible_prev_stick_negative() -> None:
    # Adjacent phase negative: current-frame down-stick alone does not decide Start0 anim-end launch.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialHi_80113838
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_HI_START_0)
    seed["animation_index"][0, 0] = np.uint32(309)
    seed["anim_frame_f32"][0, 0] = np.float32(200.0)
    out = _run_seed_one_step(seed, _mk_inputs(), _mk_inputs(main_y=-80))
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1


@pytest.mark.integration
def test_sheik_ground_vanish_platform_pass_enters_air_travel_replay_real_lock() -> None:
    # Grounded Start0 launch rejects source ground travel when ftCo_8009A134 consumes the current
    # platform floor, updates floor_skip, and falls through to the aerial travel helper.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialHi_80113838
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/ToughOutlyingChicken.msl")
    record = 11512
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_HI_START_0
    assert int(samples[record]["seed_t"]["on_ground"][0]) == 1
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "expected_action"),
    [
        (1262, ACT_SK_SPECIAL_HI_START_0),
        (1821, ACT_SK_SPECIAL_HI_START_0),
    ],
)
def test_sheik_vanish_travel_entry_spawns_smoke_article_demo_locks(
    record: int, expected_action: int
) -> None:
    # Travel entry installs fn_80112ED8, whose accessory callback spawns It_Kind_Seak_Vanish
    # through it_802B1C60 after procMap/collision in Fighter_8006C80C's accessory phase. Cover
    # both the air and ground travel entry paths from the demo fixture.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
    #   inlineA0,ftSk_SpecialHi_80113A30,fn_80112ED8,ftSk_SpecialHi_80112F48}
    # refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1C60,it_802B1D40}
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    assert int(samples[record]["seed_t"]["action_id"][0]) == expected_action
    assert int(samples[record]["ref_t1"]["action_id"][0]) in (
        ACT_SK_SPECIAL_HI_START_1,
        ACT_SK_SPECIAL_AIR_HI_START_0,
        ACT_SK_SPECIAL_AIR_HI_START_1,
    )
    assert _item_type_present(samples[record]["ref_t1"], ITEM_SHEIK_VANISH)

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == int(samples[record]["ref_t1"]["action_id"][0])
    if record == 1262:
        assert float(out["speed_air_x_self"][0]) == pytest.approx(
            float(samples[record]["ref_t1"]["speed_air_x_self"][0]), abs=1.0e-6
        )
        assert float(out["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1.0e-6)
    assert _item_type_present(out, ITEM_SHEIK_VANISH)
    ref_slot = _item_slot_for_type(samples[record]["ref_t1"], ITEM_SHEIK_VANISH)
    out_slot = _item_slot_for_type(out, ITEM_SHEIK_VANISH)
    assert float(out["items"]["timer"][out_slot]) == pytest.approx(
        float(samples[record]["ref_t1"]["items"]["timer"][ref_slot]), abs=1e-6
    )
    assert float(out["items"]["pos_x"][out_slot]) == pytest.approx(
        float(samples[record]["ref_t1"]["items"]["pos_x"][ref_slot]), abs=1e-5
    )
    assert float(out["items"]["pos_y"][out_slot]) == pytest.approx(
        float(samples[record]["ref_t1"]["items"]["pos_y"][ref_slot]), abs=1e-5
    )


@pytest.mark.integration
def test_sheik_vanish_travel_entry_can_spawn_second_smoke_article_replay_real() -> None:
    # Vanish's accessory4 callback spawns a fresh smoke article on each travel entry. Source does
    # not reject the spawn when an older It_Kind_Seak_Vanish smoke from the same owner is still
    # alive; it_802B1C60 allocates a new item and the older smoke simply continues its lifetime.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
    #   inlineA0,ftSk_SpecialHi_80113A30,fn_80112ED8}
    # refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1C60,it_802B1D40}
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl")
    record = 4592
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_0
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert _item_type_present(samples[record]["seed_t"], ITEM_SHEIK_VANISH)

    out = _run_sample_row(samples, record)
    out_slots = [
        i
        for i, item in enumerate(out["items"])
        if int(item["exists"]) != 0 and int(item["type"]) == ITEM_SHEIK_VANISH
    ]
    ref_slots = [
        i
        for i, item in enumerate(samples[record]["ref_t1"]["items"])
        if int(item["exists"]) != 0 and int(item["type"]) == ITEM_SHEIK_VANISH
    ]
    assert len(out_slots) == len(ref_slots) == 2
    assert float(out["items"][out_slots[1]]["timer"]) == pytest.approx(80.0, abs=1e-6)


@pytest.mark.integration
def test_sheik_vanish_start0_existing_smoke_does_not_duplicate_before_travel_entry() -> None:
    # Adjacent quiet frame: a live older smoke article alone is not a spawn command. The second
    # smoke appears only once Start0's Anim callback reaches the travel-entry owner.
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl")
    record = 4591
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_0
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_0

    out = _run_sample_row(samples, record)
    out_count = sum(
        1
        for item in out["items"]
        if int(item["exists"]) != 0 and int(item["type"]) == ITEM_SHEIK_VANISH
    )
    ref_count = sum(
        1
        for item in samples[record]["ref_t1"]["items"]
        if int(item["exists"]) != 0 and int(item["type"]) == ITEM_SHEIK_VANISH
    )
    assert out_count == ref_count == 1


def test_sheik_non_vanish_special_does_not_spawn_smoke_article_negative() -> None:
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_END)
    seed["animation_index"][0, 0] = np.uint32(298)
    seed["anim_frame_f32"][0, 0] = np.float32(39.0)
    out = _run(seed, [_mk_inputs()])[0]
    assert not _item_type_present(out, ITEM_SHEIK_VANISH)


def test_sheik_vanish_pending_accessory_is_runtime_only_and_clears_on_reseed() -> None:
    # Regression for runtime-only accessory4 state: reseed writes SoA fields directly instead of
    # entering a MotionState, so it must clear stale pending callbacks explicitly.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::fn_80112ED8
    import msl_binding

    stale_seed = _seed_base("sheik")
    stale_seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_AIR_HI_START_0)
    stale_seed["animation_index"][0, 0] = np.uint32(310)
    stale_seed["anim_frame_f32"][0, 0] = np.float32(8.0)

    next_seed = _seed_base("sheik")

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    stale_seed_bytes = stale_seed.view(np.uint8).reshape((1, seed_stride)).copy()
    next_seed_bytes = next_seed.view(np.uint8).reshape((1, seed_stride)).copy()
    prev = _mk_inputs().reshape((1, input_stride)).copy()
    cur = _mk_inputs().reshape((1, input_stride)).copy()
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, stale_seed_bytes)
        msl_binding.debug_set_sheik_vanish_smoke_accessory_pending(handle, 0, 0, 1)
        msl_binding.debug_set_hitlag(handle, 0, 0, 2)
        msl_binding.reseed_seed(handle, next_seed_bytes)
        msl_binding.step_input(handle, prev, cur)
        msl_binding.write_compare(handle, out_bytes)
    finally:
        msl_binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    assert int(out["action_id"][0]) == ACT_WAIT
    assert not _item_type_present(out, ITEM_SHEIK_VANISH)


def test_sheik_vanish_smoke_timer_one_destroys_article_synthetic() -> None:
    seed = _seed_base("sheik")
    seed["items"]["exists"][0, 0] = np.uint8(1)
    seed["items"]["type"][0, 0] = np.uint16(ITEM_SHEIK_VANISH)
    seed["items"]["owner"][0, 0] = np.int8(0)
    seed["items"]["timer"][0, 0] = np.float32(1.0)
    out = _run(seed, [_mk_inputs()])[0]
    assert not _item_type_present(out, ITEM_SHEIK_VANISH)


def test_sheik_vanish_smoke_timer_two_survives_and_decrements_synthetic() -> None:
    seed = _seed_base("sheik")
    seed["items"]["exists"][0, 0] = np.uint8(1)
    seed["items"]["type"][0, 0] = np.uint16(ITEM_SHEIK_VANISH)
    seed["items"]["owner"][0, 0] = np.int8(0)
    seed["items"]["timer"][0, 0] = np.float32(2.0)
    out = _run(seed, [_mk_inputs()])[0]
    assert _item_type_present(out, ITEM_SHEIK_VANISH)
    assert float(out["items"][0]["timer"]) == pytest.approx(1.0, abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "expected_action"),
    [
        (248, ACT_SK_SPECIAL_AIR_S_START),
        (1391, ACT_SK_SPECIAL_S_START),
    ],
)
def test_sheik_chain_start_spawn_frame_publishes_chain_article_demo_locks(
    record: int, expected_action: int
) -> None:
    # ftSk_SpecialS_CheckInitChain increments mv.sk.specials.x0 and spawns It_Kind_Seak_Chain
    # exactly when x0 reaches ftSeakAttributes::x1C, at the L3rdNa lb_8000B1CC position rather
    # than fighter root.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_CheckInitChain
    # refs/melee/src/melee/it/items/itseakchain.c::itSeakChain_Spawn
    # data/items/articles/fox_falco.bin::MSLITAR1 chain_spawn_part_id
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(samples[record]["seed_t"]["action_id"][0]) == expected_action
    assert int(samples[record]["seed_t"]["sheik_chain_x0_u8"][0]) == 21
    ref_slot = _item_slot_for_type(ref, ITEM_SHEIK_CHAIN)
    assert abs(float(ref["items"]["pos_x"][ref_slot]) - float(seed["pos_x"][0])) > 10.0
    assert abs(float(ref["items"]["pos_y"][ref_slot]) - float(seed["pos_y"][0])) > 5.0

    out = _run_sample_row(samples, record)
    out_slot = _item_slot_for_type(out, ITEM_SHEIK_CHAIN)
    assert float(out["items"]["pos_x"][out_slot]) == pytest.approx(
        float(ref["items"]["pos_x"][ref_slot]), abs=1e-5
    )
    assert float(out["items"]["pos_y"][out_slot]) == pytest.approx(
        float(ref["items"]["pos_y"][ref_slot]), abs=1e-5
    )


@pytest.mark.integration
@pytest.mark.parametrize("record", [494, 1441])
def test_sheik_chain_end_destroy_frame_clears_chain_article_demo_locks(record: int) -> None:
    # ftSk_SpecialS{Air}End_Anim destroys the Chain article when mv.sk.specials.x0 reaches
    # ftSeakAttributes::x28, after the earlier retract callback frame.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialSEnd_Anim,ftSk_SpecialAirSEnd_Anim}
    # refs/melee/src/melee/it/items/itseakchain.c::it_802BB20C
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_S_END
    assert int(samples[record]["seed_t"]["sheik_chain_x0_u8"][0]) == 27
    assert not _item_type_present(samples[record]["ref_t1"], ITEM_SHEIK_CHAIN)

    out = _run_sample_row(samples, record)
    assert not _item_type_present(out, ITEM_SHEIK_CHAIN)


def test_sheik_chain_start_before_spawn_frame_does_not_publish_chain_negative() -> None:
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_S_START)
    seed["animation_index"][0, 0] = np.uint32(304)
    seed["sheik_chain_x0_u8"][0, 0] = np.uint8(20)
    out = _run(seed, [_mk_inputs(buttons=B)])[0]
    assert not _item_type_present(out, ITEM_SHEIK_CHAIN)


def test_sheik_vanish_reseed_timer_controls_travel_exit() -> None:
    # ftSk_SpecialAirHiStart_1_Anim decrements hidden mv.sk.specialhi.x0. Because anim rate is
    # frozen at frame 35, replay reseed must carry the hidden timer explicitly.
    seed = _seed_base("sheik", grounded=False, pos_y=30.0)
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_AIR_HI_START_1)
    seed["animation_index"][0, 0] = np.uint32(311)
    seed["anim_frame_f32"][0, 0] = np.float32(35.0)
    seed["sheik_vanish_travel_timer_u8"][0, 0] = np.uint8(2)
    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1

    seed["sheik_vanish_travel_timer_u8"][0, 0] = np.uint8(1)
    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "seed_action"),
    [
        ("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl", 1841, ACT_SK_SPECIAL_AIR_HI_START_0),
        ("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl", 4541, ACT_SK_SPECIAL_AIR_HI_START_1),
        ("datasets/sheik/replays/validation/sheik/TenseSameHummingbird.msl", 9805, ACT_SK_SPECIAL_AIR_HI),
    ],
)
def test_sheik_vanish_air_collision_callbacks_can_cliffcatch_replay_real_lock(
    dataset_rel: str, record: int, seed_action: int
) -> None:
    # All three airborne Vanish collision callbacks run ftCliffCommon_80081298 after their
    # ground/ledge check. Keep the lock on official-suite witnesses for the callback family, not a
    # single action-id triple.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
    #   ftSk_SpecialAirHiStart_0_Coll,ftSk_SpecialAirHiStart_1_Coll,ftSk_SpecialAirHi_Coll}
    samples = _sheik_validation_samples(dataset_rel)
    assert int(samples[record]["seed_t"]["action_id"][0]) == seed_action
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_CLIFF_CATCH

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_CLIFF_CATCH


@pytest.mark.integration
def test_sheik_chain_air_start_floor_contact_swaps_to_ground_start_demo_lock() -> None:
    # ftSk_SpecialAirSStart_Coll calls ft_80081D0C. On accepted floor contact, source enters
    # grounded Chain Start at the preserved animation frame through ftSk_SpecialS_801114E4.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialAirSStart_Coll,ftSk_SpecialS_801114E4}
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    record = 254
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_S_START

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_S_START


@pytest.mark.integration
def test_sheik_chain_air_start_script_cmd0_enables_gravity_before_landing_demo_rollout() -> None:
    # ftSk_SpecialAirSStart_Phys applies gravity only after cmd_vars[0] is set by the
    # SpecialAirSStart command script. The demo's aerial Chain start stays frozen through rec246,
    # begins falling on the frame-22 script boundary at rec247, then reaches the platform landing
    # that swaps into grounded Chain Start at rec254.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialAirSStart_Anim,ftSk_SpecialAirSStart_Phys,ftSk_SpecialAirSStart_Coll,
    #   ftSk_SpecialS_801114E4}
    # data/scripts/sheik.bin::MSLFTSC1 specials_by_msid[306] set_cmd_var(idx=0,value=1)
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    assert int(samples[230]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START
    assert int(samples[246]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START
    assert float(samples[246]["ref_t1"]["speed_y_self"][0]) == pytest.approx(0.0, abs=1.0e-6)
    assert int(samples[247]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START
    assert float(samples[247]["ref_t1"]["speed_y_self"][0]) < 0.0
    assert int(samples[254]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_S_START

    rows = _run_sample_rollout_records(samples, start_record=230, records=(246, 247, 254))
    assert int(rows[246]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START
    assert float(rows[246]["pos_y"][0]) == pytest.approx(
        float(samples[246]["ref_t1"]["pos_y"][0]), abs=1.0e-6
    )
    assert float(rows[246]["speed_y_self"][0]) == pytest.approx(0.0, abs=1.0e-6)

    assert int(rows[247]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START
    assert float(rows[247]["pos_y"][0]) == pytest.approx(
        float(samples[247]["ref_t1"]["pos_y"][0]), abs=1.0e-6
    )
    assert float(rows[247]["speed_y_self"][0]) == pytest.approx(
        float(samples[247]["ref_t1"]["speed_y_self"][0]), abs=1.0e-6
    )

    assert int(rows[254]["action_id"][0]) == ACT_SK_SPECIAL_S_START
    assert int(rows[254]["ground_id"][0]) == int(samples[254]["ref_t1"]["ground_id"][0])
    assert float(rows[254]["pos_y"][0]) == pytest.approx(
        float(samples[254]["ref_t1"]["pos_y"][0]), abs=1.0e-6
    )


def test_sheik_chain_air_start_no_floor_contact_stays_air_start_negative() -> None:
    seed = _seed_base("sheik", grounded=False, pos_y=80.0)
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_AIR_S_START)
    seed["animation_index"][0, 0] = np.uint32(304)
    seed["anim_frame_f32"][0, 0] = np.float32(5.0)

    out = _run(seed, [_mk_inputs(buttons=B)])[0]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START


def test_sheik_chain_seed_lanes_use_source_x0_and_iasa_latch_order() -> None:
    import msl_binding

    char = np.full((40, 1), 7, dtype=np.uint8)
    action = np.full((40, 1), ACT_SK_SPECIAL_S_START, dtype=np.uint16)
    buttons = np.full((40, 1), B, dtype=np.uint16)
    hitlag = np.zeros((40, 1), dtype=np.uint16)
    x0, latch = msl_binding.derive_sheik_chain_seed_lanes(char, action, buttons, hitlag, 7, B, 10)
    assert int(x0[0, 0]) == 0
    assert int(x0[32, 0]) == 32
    assert int(latch[32, 0]) == 0

    # Start ground/air swaps preserve mv.sk.specials.x0 through transition_flags.
    action[20:26, 0] = ACT_SK_SPECIAL_AIR_S_START
    x0, _ = msl_binding.derive_sheik_chain_seed_lanes(char, action, buttons, hitlag, 7, B, 10)
    assert int(x0[19, 0]) == 19
    assert int(x0[20, 0]) == 20
    assert int(x0[25, 0]) == 25
    assert int(x0[26, 0]) == 26

    # Active Chain release latch is IASA-owned after Anim. A current-frame B release seeds x4 only
    # on the following row.
    action[:, 0] = ACT_SK_SPECIAL_S
    buttons[:, 0] = B
    buttons[11:, 0] = 0
    x0, latch = msl_binding.derive_sheik_chain_seed_lanes(char, action, buttons, hitlag, 7, B, 10)
    assert int(x0[10, 0]) == 10
    assert int(latch[11, 0]) == 0
    assert int(latch[12, 0]) == 1


def test_sheik_needle_seed_lane_persists_stored_count_across_non_specialn_actions() -> None:
    import msl_binding

    char = np.full((34, 1), 7, dtype=np.uint8)
    action = np.full((34, 1), ACT_SK_SPECIAL_N_LOOP, dtype=np.uint16)
    frame = np.arange(34, dtype=np.int16).reshape(34, 1)
    # Source fv.sk.x0 is stored Needle count, not a SpecialN-local timer. Cancel exits preserve it,
    # and a later fresh Neutral-B keeps the stored count instead of restarting from one.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    #   doEnter,ftSk_SpecialNLoop_Anim,ftSk_SpecialNCancel_Anim,ftSk_SpecialNEnd_Anim}
    frame[:, 0] = 0
    action[0:4, 0] = ACT_SK_SPECIAL_N_LOOP
    frame[0:4, 0] = [0, 1, 0, 1]
    action[4:8, 0] = ACT_SK_SPECIAL_N_CANCEL
    frame[4:8, 0] = [0, 1, 2, 3]
    action[8:14, 0] = ACT_WAIT
    frame[8:14, 0] = [0, 1, 2, 3, 4, 5]
    action[14:18, 0] = ACT_SK_SPECIAL_AIR_N_START
    frame[14:18, 0] = [0, 1, 2, 3]
    action[18:21, 0] = ACT_SK_SPECIAL_AIR_N_LOOP
    frame[18:21, 0] = [0, 1, 2]
    action[21:, 0] = ACT_SK_SPECIAL_AIR_N_END
    frame[21:, 0] = np.arange(13, dtype=np.int16)

    no_chain = np.zeros_like(char, dtype=np.uint8)
    count, timer = msl_binding.derive_sheik_needle_seed_lanes(char, action, frame, no_chain, 7)

    assert int(count[0, 0]) == 1
    assert int(count[2, 0]) == 2
    assert int(count[7, 0]) == 2
    assert int(count[13, 0]) == 2
    assert int(count[21, 0]) == 2
    assert int(timer[23, 0]) == 2
    assert int(count[24, 0]) == 1
    assert int(timer[26, 0]) == 5
    assert int(count[27, 0]) == 0


def test_sheik_chain_seed_lanes_freeze_anim_owned_x0_during_hitlag() -> None:
    import msl_binding

    char = np.full((12, 1), 7, dtype=np.uint8)
    action = np.full((12, 1), ACT_SK_SPECIAL_AIR_S_START, dtype=np.uint16)
    buttons = np.full((12, 1), B, dtype=np.uint16)
    hitlag = np.zeros((12, 1), dtype=np.uint16)
    hitlag[4:7, 0] = [4, 3, 2]
    hitlag[7, 0] = 1

    x0, latch = msl_binding.derive_sheik_chain_seed_lanes(char, action, buttons, hitlag, 7, B, 10)

    assert [int(v) for v in x0[:, 0]] == [0, 1, 2, 3, 4, 4, 4, 4, 5, 6, 7, 8]
    assert not latch.any()


@pytest.mark.integration
def test_sheik_chain_start_hidden_x0_enters_active_demo_lock() -> None:
    # Chain Start's visible animation frame is held at 25, but
    # ftSk_SpecialS_CheckInitChain keeps incrementing mv.sk.specials.x0 and enters active Chain
    # when x0 > ftSeakAttributes::x20.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialS_CheckInitChain,ftSk_SpecialSStart_Anim}
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")

    before = 258
    assert int(samples[before]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_S_START
    assert int(samples[before]["seed_t"]["sheik_chain_x0_u8"][0]) == 31
    assert int(samples[before]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_S_START
    before_out = _run_sample_row(samples, before)
    assert int(before_out["action_id"][0]) == ACT_SK_SPECIAL_S_START

    record = 259
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_S_START
    assert int(samples[record]["seed_t"]["sheik_chain_x0_u8"][0]) == 32
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_S
    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_S


@pytest.mark.integration
def test_sheik_chain_start_hidden_x0_hitlag_freeze_delays_active_demo_lock() -> None:
    # Chain Start's x0 is Anim-owned. Hitlag freezes Fighter_8006A360's Anim/IASA path, so the
    # hidden x0 seed lane must not count hitlag-frozen frames even while the replay-visible action
    # stays in SpecialAirSStart.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_CheckInitChain
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")

    assert int(samples[4630]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START
    assert int(samples[4630]["seed_t"]["sheik_chain_x0_u8"][0]) == 29
    assert int(samples[4630]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START
    assert int(_run_sample_row(samples, 4630)["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START

    assert int(samples[4632]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START
    assert int(samples[4632]["seed_t"]["sheik_chain_x0_u8"][0]) == 31
    assert int(samples[4632]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START
    assert int(_run_sample_row(samples, 4632)["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START

    assert int(samples[4633]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START
    assert int(samples[4633]["seed_t"]["sheik_chain_x0_u8"][0]) == 32
    assert int(samples[4633]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S
    assert int(_run_sample_row(samples, 4633)["action_id"][0]) == ACT_SK_SPECIAL_AIR_S


@pytest.mark.integration
def test_sheik_demo_chain_start_terminal_frontier_payload_and_hitlag_freeze_rollout() -> None:
    # SpecialAirSStart creates Chain fighter hitcaps from script frame 22, before the held-chain
    # x1C movement gate runs. The source article publisher can put hb2 and hb3 on the same terminal
    # frontier link; the terminal hb3 payload owns the BODY damage log, then owner hitlag freezes
    # `mv.sk.specials.x0` before Start can enter active Chain.
    # refs/melee/src/melee/it/items/itseakchain.c::it_802BCB88
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialS_UpdateHitboxes,ftSk_SpecialS_CheckInitChain}
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    rows = _run_sample_rollout_records(
        samples,
        start_record=4261,
        records=(4623, 4624, 4625, 4630, 4633, 4640),
    )

    assert int(rows[4623]["hitlag"][0]) == 0
    assert int(rows[4623]["action_id"][1]) == ACT_WAIT
    assert float(rows[4623]["percent"][1]) == 0.0

    hit = rows[4624]
    ref_hit = samples[4624]["ref_t1"]
    assert int(hit["hitlag"][0]) == int(ref_hit["hitlag"][0]) == 4
    assert int(hit["action_id"][1]) == int(ref_hit["action_id"][1]) == ACT_DAMAGE_LW_2
    assert int(hit["hitlag"][1]) == int(ref_hit["hitlag"][1]) == 6
    assert float(hit["percent"][1]) == pytest.approx(float(ref_hit["percent"][1]))

    assert int(rows[4625]["hitlag"][0]) == int(samples[4625]["ref_t1"]["hitlag"][0]) == 3
    assert int(rows[4630]["action_id"][0]) == int(samples[4630]["ref_t1"]["action_id"][0])
    assert int(rows[4633]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S
    assert int(rows[4640]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_END


@pytest.mark.integration
def test_sheik_demo_chain_retract_landing_and_flag_tail_rollout() -> None:
    # Follow-on lock for the official demo after the rec4624 Chain contact:
    # - active SpecialAirS floor contact enters aerial retract without publishing grounded state,
    # - later AttackAirB -> Landing carries the source interrupt-allowed state flag,
    # - Attack12 -> Attack100Start carries the source set_jab_rapid flag into rapid-jab start.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialAirS_Coll,ftSk_SpecialS_80111EB4,ftSk_SpecialAirSEnd_Coll}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::{
    #   ftCo_Landing_Enter,ftCo_Landing_Enter_Basic}
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80071B28
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_Attack100Start_IASA
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    rows = _run_sample_rollout_records(
        samples,
        start_record=4261,
        records=(4640, 4641, 5072, 5448, 5976),
    )

    assert int(rows[4640]["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_END
    assert int(rows[4640]["on_ground"][0]) == 0
    assert float(rows[4641]["pos_y"][0]) == pytest.approx(
        float(samples[4641]["ref_t1"]["pos_y"][0])
    )

    assert int(rows[5072]["action_id"][0]) == ACT_LANDING
    assert int(rows[5072]["state_flags"][0][0]) == int(samples[5072]["ref_t1"]["state_flags"][0][0])

    assert int(rows[5448]["action_id"][0]) == ACT_ATTACK_100_START
    assert int(rows[5448]["state_flags"][0][0]) == int(samples[5448]["ref_t1"]["state_flags"][0][0])
    assert int(rows[5976]["action_id"][0]) == ACT_ATTACK_100_START
    assert int(rows[5976]["state_flags"][0][0]) == int(samples[5976]["ref_t1"]["state_flags"][0][0])


@pytest.mark.integration
def test_sheik_chain_active_release_latch_exits_one_frame_after_b_release_demo_lock() -> None:
    # Active Chain checks the existing release latch in Anim, then IASA sets x4 from the current
    # B-held state. Therefore the B release at row 465 is not consumed until row 466.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialS_Anim,ftSk_SpecialS_IASA}
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")

    release_frame = 465
    assert int(samples[release_frame]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_S
    assert (int(samples[release_frame]["input_t"]["p"]["buttons"][0]) & B) == 0
    assert int(samples[release_frame]["seed_t"]["sheik_chain_release_latch_u8"][0]) == 0
    assert int(samples[release_frame]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_S
    release_out = _run_sample_row(samples, release_frame)
    assert int(release_out["action_id"][0]) == ACT_SK_SPECIAL_S

    record = 466
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_S
    assert int(samples[record]["seed_t"]["sheik_chain_release_latch_u8"][0]) == 1
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_S_END
    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_S_END


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "expected_ground"),
    [
        ("datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl", 2940, 4),
        ("datasets/sheik/replays/validation/sheik/TenseSameHummingbird.msl", 6243, 5),
        ("datasets/sheik/replays/validation/sheik/MixedAllQuetzal.msl", 8569, 4),
    ],
)
def test_sheik_vanish_air_end_script_cmd0_bottom_floor_landing_replay_real_lock(
    dataset_rel: str, record: int, expected_ground: int
) -> None:
    # Vanish end script cmd_vars[0] is live after frame 9, so ftSk_SpecialAirHi_Phys applies
    # FallBasic gravity before collision. The collision callback then calls ft_CheckGroundAndLedge
    # -> mpColl_800473CC; source probes show mpColl_80044628_Floor accepts the JObj ECB bottom
    # crossing and mpColl_80044838_Floor(ignore_bottom=true) snaps to LandingFallSpecial.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHi_Coll
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHi_Phys
    # refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor,
    #   mpColl_80044838_Floor}
    samples = _sheik_validation_samples(dataset_rel)
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(samples[record]["ref_t1"]["on_ground"][0]) == 1
    assert int(samples[record]["ref_t1"]["ground_id"][0]) == expected_ground

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["on_ground"][0]) == 1
    assert int(out["ground_id"][0]) == expected_ground
    assert np.isclose(float(out["pos_y"][0]), float(samples[record]["ref_t1"]["pos_y"][0]), atol=1.0e-4)


@pytest.mark.integration
def test_sheik_vanish_air_end_floor_landing_does_not_steal_cliffcatch_negative() -> None:
    # Adjacent negative: the same SpecialAirHi callback family must keep ledge contacts on
    # ftCliffCommon_80081298 instead of converting every root crossing into hard-floor landing.
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl")
    record = 7655
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_CLIFF_CATCH

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_CLIFF_CATCH


@pytest.mark.integration
def test_sheik_demo_grounded_vanish_air_travel_and_end_friction_rollout_lock() -> None:
    # ftSk_SpecialAirHiStart_0_Coll can enter airborne travel from grounded Start0 through
    # ftSk_SpecialHi_80113390 / ftSk_SpecialHi_80113A30; the entry frame publishes horizontal
    # travel velocity while vertical root displacement remains floor-owned until the next frame.
    # When the hidden x0 timer expires, ftSk_SpecialHi_80113F68 multiplies the current travel
    # self_vel by ftSeakAttributes::x54, and ftSk_SpecialAirHi_Phys then applies
    # ftCommon_8007CEF4 aerial friction via Fighter_procUpdate.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
    #   ftSk_SpecialAirHiStart_0_Coll,ftSk_SpecialHi_80113390,ftSk_SpecialHi_80113A30,
    #   ftSk_SpecialAirHiStart_1_Anim,ftSk_SpecialHi_80113F68,ftSk_SpecialAirHi_Phys}
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CEF4
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    samples = _sheik_validation_samples(
        "datasets/sheik_demo_triage/replays/validation/sheik/sheik_demo_game.msl"
    )
    rows = _run_sample_rollout_records(
        samples,
        start_record=1566,
        records=(1710, 1730, 1737, 1740),
        replay_frame_rng=True,
    )

    assert int(rows[1710]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert float(rows[1710]["pos_y"][0]) == pytest.approx(
        float(samples[1710]["ref_t1"]["pos_y"][0]), abs=1.0e-5
    )
    assert int(rows[1730]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI
    assert float(rows[1730]["speed_air_x_self"][0]) == pytest.approx(
        float(samples[1730]["ref_t1"]["speed_air_x_self"][0]), abs=1.0e-6
    )
    assert float(rows[1730]["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1.0e-6)
    assert float(rows[1737]["speed_air_x_self"][0]) == pytest.approx(
        float(samples[1737]["ref_t1"]["speed_air_x_self"][0]), abs=1.0e-6
    )
    assert int(rows[1740]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI
    assert float(rows[1740]["pos_x"][0]) == pytest.approx(
        float(samples[1740]["ref_t1"]["pos_x"][0]), abs=1.0e-5
    )


@pytest.mark.integration
def test_sheik_demo_main_floor_vanish_air_travel_integrates_vertical_immediately_lock() -> None:
    # Adjacent ground-line discriminator for the Start0 -> AirHiStart1 handoff: the soft-platform
    # entry above defers vertical displacement for one frame, but main-floor upward travel applies
    # the freshly computed self_vel.y immediately.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
    #   ftSk_SpecialAirHiStart_0_Coll,ftSk_SpecialHi_80113390,ftSk_SpecialHi_80113A30}
    # data/stages/*.bin::MSLSTG01 floor line platform flags
    samples = _sheik_validation_samples(
        "datasets/sheik_demo_triage/replays/validation/sheik/sheik_demo_game.msl"
    )
    rows = _run_sample_rollout_records(
        samples,
        start_record=1928,
        records=(2089, 2090, 2147),
        replay_frame_rng=True,
    )

    assert int(rows[2089]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert float(rows[2089]["pos_y"][0]) == pytest.approx(
        float(samples[2089]["ref_t1"]["pos_y"][0]), abs=1.0e-5
    )
    assert float(rows[2090]["pos_y"][0]) == pytest.approx(
        float(samples[2090]["ref_t1"]["pos_y"][0]), abs=1.0e-5
    )
    assert int(rows[2147]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI
    assert int(samples[2147]["ref_t1"]["on_ground"][0]) == 0
    assert int(rows[2147]["on_ground"][0]) == 0


@pytest.mark.integration
def test_sheik_demo_air_vanish_start0_low_stick_deadzone_keeps_x_static_rollout() -> None:
    # SpecialAirHiStart_0_Phys calls ftCommon_8007D268, which consumes the common preprocessed
    # stick. Low raw X values inside p_ftCommonData's deadzone must not introduce horizontal drift
    # during Vanish startup; rec8253/8255 are after the Wait-RNG row but prove this float owner.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHiStart_0_Phys
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D268,ftCommon_8007D174}
    # data/common/ft_common_data.json::lstick_deadzone_x
    samples = _sheik_validation_samples(
        "datasets/sheik_demo_triage/replays/validation/sheik/sheik_demo_game.msl"
    )
    rows = _run_sample_rollout_records(
        samples,
        start_record=8227,
        records=(8248, 8253, 8255),
        replay_frame_rng=True,
    )

    for rec in (8248, 8253, 8255):
        ref = samples[rec]["ref_t1"]
        assert int(rows[rec]["action_id"][0]) == int(ref["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_0
        assert float(rows[rec]["speed_air_x_self"][0]) == pytest.approx(0.0, abs=1e-6)
        assert float(rows[rec]["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-6)


@pytest.mark.integration
def test_sheik_vanish_air_travel_wall_angle_enters_air_end_replay_real_lock() -> None:
    # ftSk_SpecialAirHiStart_1_Coll consumes current CollData wall/ceiling contacts. When the angle
    # between the contact normal and travel velocity exceeds 90 + ftSeakAttributes::x50, source
    # enters SpecialAirHi through ftSk_SpecialHi_80113F68 before the hidden travel timer expires.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
    #   ftSk_SpecialAirHiStart_1_Coll,ftSk_SpecialHi_80113F68}
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl")
    record = 7635
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(samples[record]["seed_t"]["sheik_vanish_travel_timer_u8"][0]) > 1
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI

    out, contacts = _run_sample_row_with_contacts(samples, record)
    assert int(contacts["wall_kind"][0]) == 2
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI


@pytest.mark.integration
def test_sheik_vanish_air_travel_no_wall_contact_stays_travel_adjacent_negative() -> None:
    # Adjacent negative: the same Vanish travel episode has no wall/ceiling contact one frame
    # earlier, so the Coll callback must not end travel based on timer/action alone.
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl")
    record = 7634
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1

    out, contacts = _run_sample_row_with_contacts(samples, record)
    assert int(contacts["wall_kind"][0]) == 0
    assert int(contacts["ceiling_id"][0]) == 0xFFFF
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1


@pytest.mark.integration
def test_sheik_vanish_start1_early_platform_pass_writes_floor_skip_replay_real_lock() -> None:
    # ftSk_SpecialAirHiStart_1_Coll increments mv.sk.specialhi.xC. While xC is still below
    # ftSeakAttributes::x3C, an accepted platform contact calls ftCo_8009A134/mpUpdateFloorSkip and
    # keeps Sheik airborne in SpecialAirHiStart_1. The same source-owned CollData.floor_skip is
    # serialized into later one-step seeds so they do not re-ground on the skipped platform.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
    #   ftSk_SpecialAirHiStart_1_Anim,ftSk_SpecialAirHiStart_1_Coll}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    first_contact = 1712
    carried_skip = 1713

    assert int(samples[first_contact]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(samples[first_contact]["seed_t"]["floor_skip_segment_valid_u8"][0]) == 0
    expected_platform = int(samples[first_contact]["seed_t"]["ground_id"][0])
    assert int(samples[first_contact]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    out, colldata = _run_sample_row_with_colldata(
        samples,
        first_contact,
    )
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(out["on_ground"][0]) == 0
    assert float(out["pos_y"][0]) == pytest.approx(
        float(samples[first_contact]["ref_t1"]["pos_y"][0]), abs=1e-6
    )
    assert int(colldata["floor_skip_valid"][0]) == 1
    assert int(colldata["floor_skip_segment_id"][0]) == expected_platform

    assert int(samples[carried_skip]["seed_t"]["floor_skip_segment_valid_u8"][0]) == 1
    out = _run_sample_row(samples, carried_skip)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(out["on_ground"][0]) == 0
    assert float(out["pos_y"][0]) == pytest.approx(
        float(samples[carried_skip]["ref_t1"]["pos_y"][0]), abs=1e-6
    )


@pytest.mark.integration
def test_sheik_demo2_vanish_start1_platform_remap_keeps_source_root_snap_lock() -> None:
    # Same early-xC ftCo_8009A134 owner as rec1712 above, but this Battlefield contact resolves
    # from the carried CollData floor to the adjacent platform segment. Source mpColl publishes the
    # callback-current platform root while still rejecting the grounded SpecialHiStart_1 handoff.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHiStart_1_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/sheik_demo_game_2.msl"
    )
    record = 3095
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(samples[record]["seed_t"]["ground_id"][0]) != int(
        samples[record]["ref_t1"]["ground_id"][0]
    )

    out, colldata = _run_sample_row_with_colldata(samples, record)
    ref = samples[record]["ref_t1"]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(out["on_ground"][0]) == 0
    assert int(out["ground_id"][0]) == int(ref["ground_id"][0])
    assert float(out["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)
    assert int(colldata["floor_skip_valid"][0]) == 1
    assert int(colldata["floor_skip_segment_id"][0]) == int(ref["ground_id"][0])


@pytest.mark.integration
def test_sheik_vanish_start1_late_platform_contact_enters_grounded_negative() -> None:
    # Adjacent negative: after the xC >= ftSeakAttributes::x3C boundary, the same platform contact
    # must enter grounded SpecialHiStart_1 when no prior CollData.floor_skip is live. This prevents
    # the early-platform owner from becoming "all Vanish platform contacts stay airborne."
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    record = 1712
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1

    out = _run_sample_row(
        samples,
        record,
        mutate_seed={
            "sheik_vanish_travel_timer_u8": 2,
            "floor_skip_segment_valid_u8": 0,
            "floor_skip_segment_id_u16": 0xFFFF,
        },
    )
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_HI_START_1
    assert int(out["on_ground"][0]) == 1


@pytest.mark.integration
def test_sheik_demo2_vanish_start1_late_ground_contact_clears_horizontal_velocity_lock() -> None:
    # ftSk_SpecialAirHiStart_1_Coll late ground contact enters grounded travel through
    # ftSk_SpecialHi_801137C8. The handoff preserves the vertical travel lane for the frozen
    # grounded frame, but horizontal self/gr velocity is clear on the source row.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
    #   ftSk_SpecialAirHiStart_1_Coll,ftSk_SpecialHi_801137C8}
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/sheik_demo_game_2.msl"
    )
    record = 6873
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_HI_START_1

    out = _run_sample_row(samples, record)
    ref = samples[record]["ref_t1"]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_HI_START_1
    assert int(out["on_ground"][0]) == 1
    assert float(out["speed_air_x_self"][0]) == pytest.approx(float(ref["speed_air_x_self"][0]), abs=1e-6)
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(
        float(ref["speed_ground_x_self"][0]), abs=1e-6
    )
    assert float(out["speed_y_self"][0]) == pytest.approx(float(ref["speed_y_self"][0]), abs=1e-6)


@pytest.mark.integration
def test_sheik_vanish_start1_early_platform_pass_keeps_horizontal_travel_negative() -> None:
    # Adjacent negative: early xC platform pass-through stays airborne in SpecialAirHiStart_1, so it
    # must retain the travel horizontal velocity rather than applying the late grounded handoff clear.
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    record = 1712
    out = _run_sample_row(samples, record)
    ref = samples[record]["ref_t1"]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(out["on_ground"][0]) == 0
    assert float(out["speed_air_x_self"][0]) == pytest.approx(float(ref["speed_air_x_self"][0]), abs=1e-6)


@pytest.mark.integration
def test_sheik_vanish_cliffcatch_keeps_downheld_source_reject_replay_real_lock() -> None:
    # Adjacent negative: ftCliffCommon_80081298 rejects ledge catch while holding down past the
    # common cliff-drop threshold. The Sheik Vanish callback eligibility must not bypass that
    # source-owned input gate.
    # refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl")
    record = 4541
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_CLIFF_CATCH

    out = _run_sample_row(samples, record, mutate_input_y=-80)
    assert int(out["action_id"][0]) != ACT_CLIFF_CATCH


def test_sheik_transform_entry_enters_private_sheik_start_state() -> None:
    seed = _seed_base("sheik", grounded=False, pos_y=30.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(ACT_FALL)
    seed["speed_air_x_self"][0, 0] = np.float32(2.0)
    seed["speed_y_self"][0, 0] = np.float32(4.0)
    out = _run(seed, [_mk_inputs(buttons=B, main_y=-80)])[0]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_LW
    assert int(out["animation_index"][0]) == 315


# Sheik Down-B / Transform. The source swaps between Sheik/Zelda twin entities via
# ftCommon_8007EFC8 when transform-start ends. The runtime models the bounded demo2-visible
# same-player char/action handoff without broad Zelda specials or a full hidden Sheik twin:
# Sheik start -> Zelda finish and Zelda start -> Sheik finish, then grounded finish resolves
# through ft_8008A2BC.
# refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialLw.c
# refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialLw.c
# refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007EFC8


def test_sheik_transform_grounded_bounded_progression_freerun() -> None:
    # Grounded Down-B: Sheik start (361) -> Zelda finish (356) -> Wait.
    seed = _seed_base("sheik")
    outs = _run(seed, [_mk_inputs(buttons=B, main_y=-80)] + [_mk_inputs() for _ in range(120)])
    acts = [int(o["action_id"][0]) for o in outs]
    chars = [int(o["char_id"][0]) for o in outs]
    assert acts[0] == ACT_SK_SPECIAL_LW
    assert chars[0] == CHAR_SHEIK
    assert ACT_ZD_SPECIAL_LW_2 in acts, acts[:5]
    first_finish = acts.index(ACT_ZD_SPECIAL_LW_2)
    assert chars[first_finish] == CHAR_ZELDA
    assert acts[-1] == ACT_WAIT
    assert all(a == ACT_SK_SPECIAL_LW for a in acts[:first_finish])


def test_sheik_transform_aerial_bounded_progression_freerun() -> None:
    # Aerial Down-B (high enough to finish before landing): Sheik start (363) -> Zelda finish
    # (358) -> Fall.
    seed = _seed_base("sheik", grounded=False, pos_y=140.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(ACT_FALL)
    outs = _run(seed, [_mk_inputs(buttons=B, main_y=-80)] + [_mk_inputs() for _ in range(80)])
    acts = [int(o["action_id"][0]) for o in outs]
    chars = [int(o["char_id"][0]) for o in outs]
    assert acts[0] == ACT_SK_SPECIAL_AIR_LW
    assert chars[0] == CHAR_SHEIK
    assert ACT_ZD_SPECIAL_AIR_LW_2 in acts, acts[:5]
    assert ACT_FALL in acts  # aerial finish ends into Fall (ftZd_SpecialAirLw2_Anim -> ftCo_Fall_Enter)
    first_finish = acts.index(ACT_ZD_SPECIAL_AIR_LW_2)
    assert chars[first_finish] == CHAR_ZELDA
    assert all(a == ACT_SK_SPECIAL_AIR_LW for a in acts[:first_finish])


def test_sheik_transform_velocity_divisor_and_bounded_intangibility() -> None:
    # Entry halves velocity by attr x60/x64 (divisor 2.0). The transform start carries a BOUNDED
    # intangibility window driven by the animation's body-state track (data-driven, not a transform-
    # specific code hack): Sheik becomes intangible mid-start and is vulnerable on the entry frame, so
    # the window is real but not whole-move invulnerability.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialLw.c::ftSk_SpecialLw_Enter
    seed = _seed_base("sheik")
    seed["speed_ground_x_self"][0, 0] = np.float32(2.4)
    outs = _run(seed, [_mk_inputs(buttons=B, main_y=-80)] + [_mk_inputs() for _ in range(40)])
    assert float(outs[0]["speed_ground_x_self"][0]) == pytest.approx(1.2, abs=1e-4)
    hurts = [int(o["hurtbox_state"][0]) for o in outs]
    assert hurts[0] == 0  # vulnerable on the entry frame (no instant invuln from the C callback)
    assert any(h != 0 for h in hurts)  # a real (data-driven) intangibility window exists
    assert any(h == 0 for h in hurts[1:])  # but it is bounded, not the whole move


def test_sheik_transform_is_not_interruptible() -> None:
    # ftSk_SpecialLw_IASA / ftSk_SpecialAirLw_IASA are empty: the transform cannot be cancelled into
    # another action by holding jump/special. It stays in the transform until the anim resolves it.
    seed = _seed_base("sheik")
    frames = [_mk_inputs(buttons=B, main_y=-80)] + [_mk_inputs(buttons=B, main_y=-80) for _ in range(6)]
    outs = _run(seed, frames)
    assert all(int(o["action_id"][0]) == ACT_SK_SPECIAL_LW for o in outs)


def test_sheik_transform_aerial_lands_swaps_to_grounded_transform() -> None:
    # ftSk_SpecialAirLw_Coll: landing during the aerial transform swaps to the grounded transform
    # start (0x169) at the preserved anim frame, not a random/undefined action.
    seed = _seed_base("sheik", grounded=False, pos_y=6.0)
    seed["action_id"][0, 0] = np.uint16(ACT_FALL)
    seed["animation_index"][0, 0] = np.uint32(ACT_FALL)
    outs = _run(seed, [_mk_inputs(buttons=B, main_y=-80)] + [_mk_inputs() for _ in range(30)])
    acts = [int(o["action_id"][0]) for o in outs]
    assert acts[0] == ACT_SK_SPECIAL_AIR_LW
    assert ACT_SK_SPECIAL_LW in acts, acts  # landed -> grounded transform


def test_zelda_down_b_requires_b_edge_not_held_negative() -> None:
    # ftCo_Special{,Air}_CheckInput dispatches specials from the current-frame input edge. Holding B
    # and then moving the stick down must not re-enter Zelda transform without a new B press.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::ftCo_SpecialS_CheckInput
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    # refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialLw.c::ftZd_SpecialLw_Enter
    seed = _seed_base("zelda")
    outs = _run(seed, [_mk_inputs(buttons=B), _mk_inputs(buttons=B, main_y=-80)])
    assert int(outs[1]["action_id"][0]) not in (ACT_ZD_SPECIAL_LW, ACT_ZD_SPECIAL_AIR_LW)


@pytest.mark.integration
def test_sheik_demo2_transform_air_uses_friction_not_stick_drift_lock() -> None:
    # ftSk_SpecialAirLw_Phys uses ftCommon_Fall with transform attrs followed by
    # ftCommon_8007CEF4. It does not call ftCommon_8007D268, so the held side-stick in demo2 must
    # not add common air drift while the transform decays x velocity to zero.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialLw.c::ftSk_SpecialAirLw_Phys
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_Fall,ftCommon_8007CEF4}
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/sheik_demo_game_2.msl"
    )

    for record in (4538, 4540, 4545, 4554):
        seed = samples[record]["seed_t"]
        ref = samples[record]["ref_t1"]
        assert int(seed["action_id"][0]) == ACT_SK_SPECIAL_AIR_LW
        assert int(ref["action_id"][0]) == ACT_SK_SPECIAL_AIR_LW
        out = _run_sample_row(samples, record)
        assert float(out["speed_air_x_self"][0]) == pytest.approx(
            float(ref["speed_air_x_self"][0]), abs=1e-6
        )
        assert float(out["speed_y_self"][0]) == pytest.approx(float(ref["speed_y_self"][0]), abs=1e-6)

    rows = _run_sample_rollout_records(
        samples,
        start_record=4538,
        records=(4554, 5097),
        replay_frame_rng=True,
    )
    assert int(rows[4554]["action_id"][0]) == ACT_SK_SPECIAL_AIR_LW
    assert float(rows[4554]["speed_air_x_self"][0]) == pytest.approx(0.0, abs=1e-6)
    assert int(rows[5097]["action_id"][0]) == int(samples[5097]["ref_t1"]["action_id"][0])


def test_sheik_transform_air_friction_does_not_disable_vanish_windup_stick_drift_negative() -> None:
    # Adjacent callback-family negative: Vanish windup remains ftCommon_8007D268 common-air drift.
    # The transform friction path must not become a broad "all Sheik aerial specials ignore stick"
    # rule.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHiStart_0_Phys
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D268,ftCommon_8007D174}
    seed = _seed_base("sheik", grounded=False, pos_y=40.0)
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_AIR_HI_START_0)
    seed["animation_index"][0, 0] = np.uint32(311)
    seed["speed_air_x_self"][0, 0] = np.float32(0.22)
    seed["speed_y_self"][0, 0] = np.float32(0.44)
    out = _run(seed, [_mk_inputs(main_x=-80)])[0]
    assert float(out["speed_air_x_self"][0]) < 0.18


@pytest.mark.integration
def test_sheik_demo2_grounded_needle_charge_uses_ground_friction_lock() -> None:
    # ftSk_SpecialNStart/Loop grounded Phys callbacks use ft_80084F3C. Demo2 enters grounded
    # Needle charge with a small carried run velocity; source friction decays that velocity to zero
    # before the later platform run, preventing the rec5097 edge fall.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    #   ftSk_SpecialNStart_Phys,ftSk_SpecialNLoop_Phys}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/sheik_demo_game_2.msl"
    )

    for record in (5016, 5017):
        seed = samples[record]["seed_t"]
        ref = samples[record]["ref_t1"]
        assert int(seed["action_id"][0]) in (ACT_SK_SPECIAL_N_START, ACT_SK_SPECIAL_N_LOOP)
        out = _run_sample_row(samples, record)
        assert int(out["action_id"][0]) == int(ref["action_id"][0])
        assert float(out["speed_ground_x_self"][0]) == pytest.approx(
            float(ref["speed_ground_x_self"][0]), abs=1e-6
        )
        assert float(out["speed_air_x_self"][0]) == pytest.approx(
            float(ref["speed_air_x_self"][0]), abs=1e-6
        )

    rows = _run_sample_rollout_records(
        samples,
        start_record=4538,
        records=(5017, 5097),
        replay_frame_rng=True,
    )
    assert float(rows[5017]["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-6)
    assert int(rows[5097]["action_id"][0]) == int(samples[5097]["ref_t1"]["action_id"][0])
    assert int(rows[5097]["on_ground"][0]) == int(samples[5097]["ref_t1"]["on_ground"][0])


def test_sheik_transform_bounded_same_player_swap_action_space() -> None:
    # Explicit Zelda scope boundary: the same-player transform may enter only the Zelda transform
    # finish actions needed by this replay, then common terminal states. Other Zelda specials remain
    # unsupported until a replay reaches them.
    allowed = {
        ACT_SK_SPECIAL_LW,
        ACT_SK_SPECIAL_AIR_LW,
        ACT_ZD_SPECIAL_LW_2,
        ACT_ZD_SPECIAL_AIR_LW_2,
        ACT_WAIT,
        ACT_FALL,
        ACT_FALL_SPECIAL,
        ACT_LANDING,
        ACT_LANDING_FALL_SPECIAL,
    }

    def all_acts(seed, frames):
        return {int(o["action_id"][0]) for o in _run(seed, frames)}

    g = _seed_base("sheik")
    g_acts = all_acts(g, [_mk_inputs(buttons=B, main_y=-80)] + [_mk_inputs() for _ in range(120)])
    a = _seed_base("sheik", grounded=False, pos_y=140.0)
    a["action_id"][0, 0] = np.uint16(ACT_FALL)
    a["animation_index"][0, 0] = np.uint32(ACT_FALL)
    a_acts = all_acts(a, [_mk_inputs(buttons=B, main_y=-80)] + [_mk_inputs() for _ in range(120)])
    for act in g_acts | a_acts:
        assert act in allowed, f"transform produced out-of-policy action id {act}"
    assert g_acts & {ACT_WAIT}
    assert a_acts & {ACT_FALL, ACT_LANDING, ACT_WAIT}


@pytest.mark.integration
def test_sheik_demo2_transform_swap_flags_and_wait_iasa_locks() -> None:
    # Replay-real locks for the transform handoff that supports the second demo:
    # - first Sheik start -> Zelda aerial finish uses a clear hidden Zelda twin fp+0x2218 byte.
    # - later Sheik start -> Zelda aerial finish carries the prior hidden Zelda twin fp+0x2218_b0.
    # - Zelda start -> Sheik grounded finish clears that bit.
    # - grounded Sheik finish anim-end uses ft_8008A2BC -> Wait_IASA, so a held run stick enters Run
    #   in the same fighter proc instead of serializing a bare Wait.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialLw.c::{
    #   ftSk_SpecialLw_Anim,ftSk_SpecialLw2_Anim,fn_8011412C}
    # refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialLw.c::{
    #   ftZd_SpecialLw_Anim,ftZd_SpecialLw_8013AEAC,ftZd_SpecialLw_8013B4D8}
    # refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/sheik_demo_game_2.msl"
    )

    first_zelda_finish = _run_sample_row(samples, 4070)
    assert int(first_zelda_finish["char_id"][0]) == CHAR_ZELDA
    assert int(first_zelda_finish["action_id"][0]) == ACT_ZD_SPECIAL_AIR_LW_2
    assert int(first_zelda_finish["state_flags"][0][0]) == 0

    zelda_finish = _run_sample_row(samples, 4570)
    assert int(zelda_finish["char_id"][0]) == CHAR_ZELDA
    assert int(zelda_finish["action_id"][0]) == ACT_ZD_SPECIAL_AIR_LW_2
    assert int(zelda_finish["state_flags"][0][0]) == 0x80

    sheik_finish = _run_sample_row(samples, 4679)
    assert int(sheik_finish["char_id"][0]) == CHAR_SHEIK
    assert int(sheik_finish["action_id"][0]) == ACT_SK_SPECIAL_LW_2
    assert int(sheik_finish["state_flags"][0][0]) == 0

    run_exit = _run_sample_row(samples, 4389)
    assert int(run_exit["action_id"][0]) == ACT_WALK_SLOW
    assert int(run_exit["action_frame"][0]) == 1


def test_sheik_vanish_consumes_jumps_freerun() -> None:
    # Up-B (Vanish) commits Sheik's jumps: at the travel/liftoff the source AS_SheikUpBTravelGround
    # (inlineA0) sets fp->x1968_jumpsUsed = max_jumps, i.e. jumps_left -> 0, so she cannot double-jump
    # out of the recovery. refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::inlineA0
    seed = _seed_base("sheik")
    outs = _run(
        seed,
        [_mk_inputs(buttons=B, main_y=80)] + [_mk_inputs(main_y=80) for _ in range(44)],
    )
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_SK_SPECIAL_HI_START_0 in acts, acts[:4]  # entered Vanish
    # Once she reaches the airborne/travel Vanish phases (past the grounded windup), jumps_left == 0.
    vanish_committed = [
        o for o in outs if int(o["action_id"][0]) in (357, 358, 359, 360)
    ]
    assert vanish_committed, acts
    assert all(int(o["jumps_left"][0]) == 0 for o in vanish_committed)


def test_sheik_normal_jump_does_not_zero_jumps_adjacent_negative() -> None:
    # Adjacent negative: a normal grounded jump only spends the ground jump (jumps_left -> max-1 >= 1 of
    # midair jumps remain); it is NOT forced to 0. The zeroing is specific to the Up-B commit, not any
    # ground->air transition.
    seed = _seed_base("sheik")
    outs = _run(seed, [_mk_inputs(buttons=0x0400)] + [_mk_inputs() for _ in range(8)])  # X = jump
    airborne = [
        o for o in outs if int(o["on_ground"][0]) == 0 and int(o["action_id"][0]) in (25, 29, 32)
    ]
    assert airborne, [int(o["action_id"][0]) for o in outs]
    assert all(int(o["jumps_left"][0]) >= 1 for o in airborne)


def test_sheik_neutral_b_release_enters_end_instead_of_looping_forever() -> None:
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_LOOP)
    seed["animation_index"][0, 0] = np.uint32(296)
    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_N_END

    air_seed = _seed_base("sheik", grounded=False, pos_y=30.0)
    air_seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_AIR_N_LOOP)
    air_seed["animation_index"][0, 0] = np.uint32(300)
    air_out = _run(air_seed, [_mk_inputs()])[0]
    assert int(air_out["action_id"][0]) == ACT_SK_SPECIAL_AIR_N_END


def test_sheik_needle_loop_cancel_uses_synthesized_lr_trigger_edge() -> None:
    # ftSk_SpecialN.c::doIasa checks held B plus `input.x668 & HSD_PAD_LR`; x668 includes the
    # synthesized analog-trigger edge, not only digital L/R button bits.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::doIasa
    # refs/melee/src/melee/ft/fighter.c (input lane build and x668 edge construction)
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_LOOP)
    seed["animation_index"][0, 0] = np.uint32(297)
    cur = _mk_inputs(buttons=B, r=213)
    out = _run_seed_one_step(seed, _mk_inputs(buttons=B), cur)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_N_CANCEL


def test_sheik_needle_loop_cancel_does_not_retrigger_from_held_lr_negative() -> None:
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_LOOP)
    seed["animation_index"][0, 0] = np.uint32(297)
    held = _mk_inputs(buttons=B, r=213)
    out = _run_seed_one_step(seed, held, held)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_N_LOOP


def test_sheik_neutral_b_start_anim_end_released_b_runs_loop_iasa_same_frame() -> None:
    # Start_Anim enters Loop on animation end; destination Loop_IASA then observes released B and
    # enters End in the same Fighter proc.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    #   ftSk_SpecialAirNStart_Anim,ftSk_SpecialAirNLoop_IASA}
    seed = _seed_base("sheik", grounded=False, pos_y=30.0)
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_AIR_N_START)
    seed["animation_index"][0, 0] = np.uint32(299)
    seed["anim_frame_f32"][0, 0] = np.float32(999.0)
    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_N_END


def test_sheik_neutral_b_start_anim_end_held_b_stays_in_loop() -> None:
    seed = _seed_base("sheik", grounded=False, pos_y=30.0)
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_AIR_N_START)
    seed["animation_index"][0, 0] = np.uint32(299)
    seed["anim_frame_f32"][0, 0] = np.float32(999.0)
    out = _run(seed, [_mk_inputs(buttons=B)])[0]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_N_LOOP


def test_sheik_ground_needle_cancel_anim_end_runs_destination_wait_guard_iasa() -> None:
    # ftSk_SpecialNCancel_Anim calls ft_8008A2BC at anim end. That reaches grounded Wait and the
    # destination Wait_IASA can immediately enter GuardOn through ftCo_80091A4C.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::ftSk_SpecialNCancel_Anim
    # refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_CANCEL)
    seed["animation_index"][0, 0] = np.uint32(298)
    seed["anim_frame_f32"][0, 0] = np.float32(999.0)
    out = _run(seed, [_mk_inputs(r=255)])[0]
    assert int(out["action_id"][0]) == ACT_GUARD_ON


def test_sheik_ground_needle_cancel_anim_end_no_shield_returns_wait_negative() -> None:
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_CANCEL)
    seed["animation_index"][0, 0] = np.uint32(298)
    seed["anim_frame_f32"][0, 0] = np.float32(999.0)
    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["action_id"][0]) == ACT_WAIT


def test_sheik_ground_needle_end_anim_end_runs_destination_wait_tail_synthetic() -> None:
    # ftSk_SpecialNEnd_Anim also calls ft_8008A2BC. The destination Wait_IASA locomotion tail
    # consumes down-stick in the same source proc instead of exposing a one-frame Wait.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::ftSk_SpecialNEnd_Anim
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_END)
    seed["animation_index"][0, 0] = np.uint32(298)
    seed["anim_frame_f32"][0, 0] = np.float32(999.0)
    out = _run(seed, [_mk_inputs(main_y=-80)])[0]
    assert int(out["action_id"][0]) == ACT_SQUAT


@pytest.mark.integration
def test_sheik_ground_vanish_end_anim_end_turns_from_destination_wait_demo_lock() -> None:
    # ftSk_SpecialHi_Anim ends through ft_8008A2BC. The destination Wait_IASA tail can immediately
    # enter Turn when the stick crosses the turn threshold. During the grounded end, ftSk_SpecialHi_Phys
    # applies ft_80084F3C ground friction so the launch gr_vel decays to zero before the anim end.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
    #   ftSk_SpecialHi_Phys,ftSk_SpecialHi_Anim}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C
    # refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
    samples = _sheik_validation_samples(
        "datasets/sheik_demo_triage/replays/validation/sheik/sheik_demo_game.msl"
    )
    rows = _run_sample_rollout_records(
        samples,
        start_record=1840,
        records=(1841, 1850, 1880),
        replay_frame_rng=True,
    )

    assert int(rows[1841]["action_id"][0]) == ACT_SK_SPECIAL_HI
    assert float(rows[1841]["speed_ground_x_self"][0]) == pytest.approx(
        float(samples[1841]["ref_t1"]["speed_ground_x_self"][0]), abs=1.0e-6
    )
    assert float(rows[1850]["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1.0e-6)
    assert int(samples[1880]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_HI
    assert int(samples[1880]["ref_t1"]["action_id"][0]) == ACT_TURN
    assert int(rows[1880]["action_id"][0]) == ACT_TURN
    assert float(rows[1880]["pos_x"][0]) == pytest.approx(
        float(samples[1880]["ref_t1"]["pos_x"][0]), abs=1.0e-5
    )


@pytest.mark.integration
def test_sheik_ground_vanish_end_anim_end_can_remain_wait_demo_lock() -> None:
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    record = 1986
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_HI
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_WAIT

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_WAIT


@pytest.mark.integration
def test_sheik_ground_chain_end_anim_end_squats_from_destination_wait_demo_lock() -> None:
    # ftSk_SpecialSEnd_Anim calls ft_8008A2BC. The same destination Wait_IASA owner applies to
    # terminal Chain and consumes the held down-stick as Squat.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialSEnd_Anim
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    record = 4680
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_S_END
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SQUAT

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_SQUAT


def test_sheik_air_vanish_end_anim_end_does_not_run_ground_wait_tail_negative() -> None:
    # Aerial terminal Sheik specials do not use the grounded ft_8008A2BC destination-Wait tail.
    # Vanish Air End follows ftCo_80096900 into FallSpecial instead.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHi_Anim
    seed = _seed_base("sheik", grounded=False, pos_y=30.0)
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_AIR_HI)
    seed["animation_index"][0, 0] = np.uint32(312)
    seed["anim_frame_f32"][0, 0] = np.float32(999.0)
    out = _run(seed, [_mk_inputs(main_y=-80)])[0]
    assert int(out["action_id"][0]) == ACT_FALL_SPECIAL


@pytest.mark.integration
def test_sheik_ground_needle_cancel_to_guardon_replay_real_lock() -> None:
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/StiffLustrousZebra.msl")
    record = 4085
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_N_CANCEL
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_GUARD_ON

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_GUARD_ON


@pytest.mark.integration
def test_sheik_demo_ground_needle_start_anim_spawns_held_article_replay_real() -> None:
    # ftSk_SpecialNStart_Anim creates the held Needle article with it_802B19AC before entering
    # Loop. Slippi's same-frame item publication is the held-item spawn position, not a later hand
    # model transform.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::ftSk_SpecialNStart_Anim
    # refs/melee/src/melee/it/items/itseakneedleheld.c::it_802B19AC
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    record = 95
    ref = samples[record]["ref_t1"]
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_N_START
    assert int(ref["action_id"][0]) == ACT_SK_SPECIAL_N_LOOP
    assert int(ref["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_HELD

    out = _run_sample_row(samples, record)
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_HELD
    assert int(out["items"]["spawn_id"][0]) == int(ref["items"]["spawn_id"][0])
    assert float(out["items"]["pos_x"][0]) == pytest.approx(float(ref["items"]["pos_x"][0]))
    assert float(out["items"]["pos_y"][0]) == pytest.approx(float(ref["items"]["pos_y"][0]))


@pytest.mark.integration
def test_sheik_demo_air_needle_start_anim_spawns_held_article_replay_real() -> None:
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    record = 545
    ref = samples[record]["ref_t1"]
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_N_START
    assert int(ref["action_id"][0]) == ACT_SK_SPECIAL_AIR_N_LOOP
    assert int(ref["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_HELD

    out = _run_sample_row(samples, record)
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_HELD
    assert float(out["items"]["pos_x"][0]) == pytest.approx(float(ref["items"]["pos_x"][0]))
    assert float(out["items"]["pos_y"][0]) == pytest.approx(float(ref["items"]["pos_y"][0]))


@pytest.mark.integration
@pytest.mark.parametrize("record", [2838, 3193])
def test_sheik_demo_needle_start_spawns_held_before_same_frame_end_iasa_replay_real(
    record: int,
) -> None:
    # Start_Anim creates the held Needle article before entering Loop; Loop IASA can immediately
    # enter End on B release in the same fighter proc, but the held article is still published.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    #   ftSk_SpecialNStart_Anim,ftSk_SpecialNLoop_IASA,ftSk_SpecialAirNStart_Anim,
    #   ftSk_SpecialAirNLoop_IASA}
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][0]) == ACT_SK_SPECIAL_N_START
    assert int(ref["action_id"][0]) == ACT_SK_SPECIAL_N_END
    assert int(ref["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_HELD

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_N_END
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_HELD


@pytest.mark.integration
def test_sheik_demo_needle_end_anim_first_latch_spawns_thrown_article_replay_real() -> None:
    # ftSk_SpecialNEnd_Anim arms mv.sk.specialn.x4 at timer 2, and accessory4_cb shootNeedles
    # consumes it to spawn one thrown Needle. The row locks the source latch path and the no
    # same-frame item-motion publication boundary.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    #   ftSk_SpecialNEnd_Anim,shootNeedles}
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::it_802AFD8C
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    record = 118
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][0]) == ACT_SK_SPECIAL_N_END
    assert int(seed["sheik_needle_count_u8"][0]) == 2
    assert int(seed["sheik_needle_specialn_timer_u8"][0]) == 2
    assert int(ref["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_THROWN

    out = _run_sample_row(samples, record)
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_THROWN
    assert int(out["items"]["spawn_id"][0]) == int(ref["items"]["spawn_id"][0])
    assert float(out["items"]["timer"][0]) == pytest.approx(float(ref["items"]["timer"][0]))
    assert float(out["items"]["pos_x"][0]) == pytest.approx(float(ref["items"]["pos_x"][0]))
    assert float(out["items"]["pos_y"][0]) == pytest.approx(float(ref["items"]["pos_y"][0]))
    assert float(out["items"]["vel_x"][0]) == pytest.approx(float(ref["items"]["vel_x"][0]))


@pytest.mark.integration
def test_sheik_demo_needle_shoot_uses_replay_frame_rng_at_accessory_phase() -> None:
    # The second Needle's vertical jitter is owned by shootNeedles in the destination frame's
    # accessory/item phase, after ftSk_SpecialNEnd_Anim arms mv.sk.specialn.x4. Dolphin
    # MSL_SHEIK_NEEDLE_PROBE traces on this row show HSD_Randi(9) enters at shootNeedles with
    # ref_t1.frame_pre_random_seed, not the seed_t post-frame stream.
    # refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{ftSk_SpecialNEnd_Anim,shootNeedles}
    # refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    record = 121
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][0]) == ACT_SK_SPECIAL_N_END
    assert int(seed["sheik_needle_count_u8"][0]) == 1
    assert int(seed["sheik_needle_specialn_timer_u8"][0]) == 5
    assert int(ref["items"]["exists"][1]) == 1
    assert int(ref["items"]["type"][1]) == ITEM_SHEIK_NEEDLE_THROWN

    out = _run_sample_row(samples, record, replay_frame_rng=True)
    assert int(out["items"]["exists"][1]) == 1
    assert int(out["items"]["type"][1]) == ITEM_SHEIK_NEEDLE_THROWN
    assert int(out["items"]["spawn_id"][1]) == int(ref["items"]["spawn_id"][1])
    assert float(out["items"]["pos_y"][1]) == pytest.approx(float(ref["items"]["pos_y"][1]))
    assert float(out["items"]["vel_x"][1]) == pytest.approx(float(ref["items"]["vel_x"][1]))


@pytest.mark.integration
@pytest.mark.parametrize(("record", "slot", "stored_count"), [(4925, 1, 2), (4928, 2, 1)])
def test_sheik_demo_needle_stored_count_survives_cancel_gap_then_shoots_volley(
    record: int, slot: int, stored_count: int
) -> None:
    # Earlier rows in this replay charge to three, cancel to Wait, move, then start Neutral-B
    # again. Source `fv.sk.x0` persists through that non-SpecialN gap, so the later End volley
    # still publishes multiple thrown Needles.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    #   doEnter,ftSk_SpecialNCancel_Anim,ftSk_SpecialNEnd_Anim,shootNeedles}
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][0]) == ACT_SK_SPECIAL_AIR_N_END
    assert int(seed["sheik_needle_count_u8"][0]) == stored_count
    assert int(ref["items"]["exists"][slot]) == 1
    assert int(ref["items"]["type"][slot]) == ITEM_SHEIK_NEEDLE_THROWN

    out = _run_sample_row(samples, record, replay_frame_rng=True)
    assert int(out["items"]["exists"][slot]) == 1
    assert int(out["items"]["type"][slot]) == ITEM_SHEIK_NEEDLE_THROWN
    assert int(out["items"]["spawn_id"][slot]) == int(ref["items"]["spawn_id"][slot])
    assert float(out["items"]["timer"][slot]) == pytest.approx(float(ref["items"]["timer"][slot]))
    assert float(out["items"]["vel_x"][slot]) == pytest.approx(float(ref["items"]["vel_x"][slot]))


@pytest.mark.integration
def test_sheik_demo_needle_shoot_plain_step_does_not_pull_replay_frame_rng_negative() -> None:
    # Adjacent seed-owner negative: if the replay hidden stream reconstruction is absent, normal
    # step_input must not synthesize a future replay seed. It consumes the seed snapshot's current
    # HSD stream, which is exactly why preprocessing promotes this source-owned row.
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/sheik_demo_game.msl"
    ).copy()
    record = 121
    samples[record]["seed_t"]["frame_pre_random_seed"] = samples[record - 1]["ref_t1"][
        "frame_pre_random_seed"
    ]
    ref = samples[record]["ref_t1"]
    out = _run_sample_row(samples, record)
    assert int(out["items"]["exists"][1]) == 1
    assert int(out["items"]["type"][1]) == ITEM_SHEIK_NEEDLE_THROWN
    assert float(out["items"]["pos_y"][1]) != pytest.approx(float(ref["items"]["pos_y"][1]))


@pytest.mark.integration
def test_sheik_demo_air_needle_end_uses_ft80084eec_no_stick_drift_rollout() -> None:
    # Aerial Needle end Phys is ft_80084EEC: gravity + horizontal air friction, with no
    # ftCommon_8007D268 stick drift. In this demo, live stick X on rec776/777 used to move Sheik
    # after self_vel.x had decayed to zero; that upstream X error made rec872 miss the third
    # DownWait pushbox nudge and rec888 choose DamageFlyN instead of source DamageFlyLw.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::ftSk_SpecialAirNEnd_Phys
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084EEC
    samples = _sheik_validation_samples(
        "datasets/sheik_demo_triage/replays/validation/sheik/sheik_demo_game.msl"
    )
    rows = _run_sample_rollout_records(
        samples,
        start_record=582,
        records=(775, 776, 777, 778, 872, 888),
        replay_frame_rng=True,
    )

    for rec in (775, 776, 777):
        ref = samples[rec]["ref_t1"]
        out = rows[rec]
        assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_SK_SPECIAL_AIR_N_END
        assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-6)

    ref_landing = samples[778]["ref_t1"]
    assert int(rows[778]["action_id"][0]) == int(ref_landing["action_id"][0]) == ACT_LANDING
    assert float(rows[778]["pos_x"][0]) == pytest.approx(float(ref_landing["pos_x"][0]), abs=1e-6)

    ref_nudge = samples[872]["ref_t1"]
    assert float(rows[872]["pos_x"][1]) == pytest.approx(float(ref_nudge["pos_x"][1]), abs=1e-6)

    ref_hit = samples[888]["ref_t1"]
    assert int(rows[888]["action_id"][1]) == int(ref_hit["action_id"][1]) == 89  # DamageFlyLw
    assert int(rows[888]["hitlag"][1]) == int(ref_hit["hitlag"][1]) == 7
    assert int(rows[888]["hitstun"][1]) == int(ref_hit["hitstun"][1]) == 32
    assert float(rows[888]["percent"][1]) == pytest.approx(float(ref_hit["percent"][1]), abs=1e-6)


@pytest.mark.integration
def test_sheik_demo2_dash_sideb_entry_runs_dash_terminal_scalar_lock() -> None:
    # Dash_IASA calls ftCo_SpecialS_CheckInput, then resumes after the Side-B motion-state entry and
    # applies Dash's terminal velocity scalar before SpecialSStart_Phys applies ground friction.
    # This rec329 deterministic float owner is before the first Wait animation RNG fork in demo2.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{ftCo_SpecialS_CheckInput,doEnter}
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialS_Enter,ftSk_SpecialSStart_Phys}
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/sheik_demo_game_2.msl"
    )
    out = _run_sample_row(samples, 329, replay_frame_rng=True)
    ref = samples[329]["ref_t1"]

    assert int(samples[329]["seed_t"]["action_id"][0]) == ACT_DASH
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_SK_SPECIAL_S_START
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(
        float(ref["speed_ground_x_self"][0]), abs=1e-6
    )
    assert float(out["speed_air_x_self"][0]) == pytest.approx(
        float(ref["speed_air_x_self"][0]), abs=1e-6
    )
    assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-6)


@pytest.mark.integration
def test_sheik_demo_ground_chain_start_damps_run_velocity_rollout_float_lock() -> None:
    # Grounded Side-B entry runs the common ftCo_SpecialS doEnter bundle before Sheik's Chain enter:
    # gr_vel is damped by co_attrs.xB8, then SpecialSStart_Phys applies ft_80084F3C ground friction
    # on the same frame. This closes the rec1369 float-only divergence and its rec1474 pos_x tail,
    # and remains an adjacent negative for Dash-only terminal-scalar continuation.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::{ftCo_SpecialS_CheckInput,doEnter}
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialS_Enter,ftSk_SpecialSStart_Phys}
    # data/characters/sheik.json::side_special_ground_entry_vel_mul
    samples = _sheik_validation_samples(
        "datasets/sheik_demo_triage/replays/validation/sheik/sheik_demo_game.msl"
    )
    rows = _run_sample_rollout_records(
        samples,
        start_record=582,
        records=(1368, 1369, 1372, 1474),
        replay_frame_rng=True,
    )

    ref_pre = samples[1368]["ref_t1"]
    assert int(rows[1368]["action_id"][0]) == int(ref_pre["action_id"][0]) == 21
    assert int(samples[1369]["seed_t"]["action_id"][0]) == ACT_RUN
    assert float(rows[1368]["speed_ground_x_self"][0]) == pytest.approx(
        float(ref_pre["speed_ground_x_self"][0]), abs=1e-6
    )

    ref_entry = samples[1369]["ref_t1"]
    assert int(rows[1369]["action_id"][0]) == int(ref_entry["action_id"][0]) == ACT_SK_SPECIAL_S_START
    assert float(rows[1369]["speed_ground_x_self"][0]) == pytest.approx(
        float(ref_entry["speed_ground_x_self"][0]), abs=1e-6
    )
    assert float(rows[1369]["speed_air_x_self"][0]) == pytest.approx(
        float(ref_entry["speed_air_x_self"][0]), abs=1e-6
    )
    assert float(rows[1369]["pos_x"][0]) == pytest.approx(float(ref_entry["pos_x"][0]), abs=1e-6)

    ref_stop = samples[1372]["ref_t1"]
    assert float(rows[1372]["speed_ground_x_self"][0]) == pytest.approx(0.0, abs=1e-6)
    assert float(rows[1372]["pos_x"][0]) == pytest.approx(float(ref_stop["pos_x"][0]), abs=1e-6)

    ref_tail = samples[1474]["ref_t1"]
    assert int(rows[1474]["action_id"][0]) == int(ref_tail["action_id"][0]) == ACT_TURN
    assert float(rows[1474]["pos_x"][0]) == pytest.approx(float(ref_tail["pos_x"][0]), abs=1e-5)


@pytest.mark.integration
def test_sheik_demo_runbrake_freeze_latch_resumes_one_aobj_tick_later_rollout() -> None:
    # RunBrake's cmd_vars[1] freeze is not a stateless speed predicate:
    # ftCo_RunBrake_Anim sets mv.co.runbrake.x0 when |gr_vel| >= x42C, keeps the AObj frozen
    # through the first <= x42C callback frame, then resumes on the next AObj tick. In the Sheik
    # demo this keeps rec3156 frozen, resumes on rec3157, and exits to Wait on rec3163.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::{
    #   ftCo_RunBrake_Enter,ftCo_RunBrake_Anim}
    # data/scripts/sheik.bin::MSLFTSC1 ftCo_SM_RunBrake set_cmd_var(idx=1,value=1)
    # data/common/ft_common_data.json::runbrake_anim_freeze_speed_threshold
    samples = _sheik_validation_samples(
        "datasets/sheik_demo_triage/replays/validation/sheik/sheik_demo_game.msl"
    )
    rows = _run_sample_rollout_records(
        samples,
        start_record=3100,
        records=(3153, 3155, 3156, 3157, 3162, 3163),
        replay_frame_rng=True,
    )

    for rec in (3153, 3155, 3156):
        ref = samples[rec]["ref_t1"]
        assert int(rows[rec]["action_id"][0]) == int(ref["action_id"][0]) == ACT_RUN_BRAKE
        assert int(rows[rec]["action_frame"][0]) == int(ref["action_frame"][0]) == 11

    ref_resume = samples[3157]["ref_t1"]
    assert int(rows[3157]["action_id"][0]) == int(ref_resume["action_id"][0]) == ACT_RUN_BRAKE
    assert int(rows[3157]["action_frame"][0]) == int(ref_resume["action_frame"][0]) == 12

    ref_tail = samples[3162]["ref_t1"]
    assert int(rows[3162]["action_id"][0]) == int(ref_tail["action_id"][0]) == ACT_RUN_BRAKE
    assert int(rows[3162]["action_frame"][0]) == int(ref_tail["action_frame"][0]) == 17

    ref_wait = samples[3163]["ref_t1"]
    assert int(rows[3163]["action_id"][0]) == int(ref_wait["action_id"][0]) == ACT_WAIT
    assert int(rows[3163]["action_frame"][0]) == int(ref_wait["action_frame"][0]) == 0


@pytest.mark.integration
def test_sheik_demo_kneebend_escapeair_landing_fallspecial_publishes_substep_root() -> None:
    # Fresh KneeBend -> Jump -> EscapeAir reaches EscapeAir_Coll in the same fighter proc. The
    # floor hit enters LandingFallSpecial and publishes the mpColl_800471F8 first-substep root on
    # the entry row; the full EscapeAir self velocity remains the post-frame velocity.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
    samples = _sheik_validation_samples(
        "datasets/sheik_demo_triage/replays/validation/sheik/sheik_demo_game.msl"
    )

    out = _run_sample_row(samples, 3497)
    ref = samples[3497]["ref_t1"]
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 0
    assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-6)
    assert float(out["speed_ground_x_self"][0]) == pytest.approx(
        float(ref["speed_ground_x_self"][0]), abs=1e-6
    )

    stale_prev_history = _run_sample_row(
        samples,
        3497,
        mutate_seed={"seed_prev_action_id": ACT_WAIT},
    )
    assert int(stale_prev_history["action_id"][0]) == ACT_LANDING_FALL_SPECIAL
    assert float(stale_prev_history["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-6)

    no_frame_start_kneebend = _run_sample_row(
        samples,
        3497,
        mutate_seed={"action_id": ACT_FALL},
    )
    assert int(no_frame_start_kneebend["action_id"][0]) != ACT_LANDING_FALL_SPECIAL
    assert float(no_frame_start_kneebend["pos_x"][0]) != pytest.approx(
        float(ref["pos_x"][0]), abs=1e-3
    )


@pytest.mark.integration
def test_sheik_demo_kneebend_escapeair_substep_root_keeps_later_landing_rollout() -> None:
    samples = _sheik_validation_samples(
        "datasets/sheik_demo_triage/replays/validation/sheik/sheik_demo_game.msl"
    )
    rows = _run_sample_rollout_records(
        samples,
        start_record=3008,
        records=(3497, 3971, 4088, 4089),
        replay_frame_rng=False,
    )

    for rec in (3497, 3971, 4088, 4089):
        ref = samples[rec]["ref_t1"]
        out = rows[rec]
        assert int(out["action_id"][0]) == int(ref["action_id"][0]), rec
        assert int(out["action_frame"][0]) == int(ref["action_frame"][0]), rec
        assert int(out["on_ground"][0]) == int(ref["on_ground"][0]), rec
        assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-5)


def test_sheik_needle_start_without_hidden_count_does_not_spawn_held_article_negative() -> None:
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_START)
    seed["animation_index"][0, 0] = np.uint32(295)
    seed["anim_frame_f32"][0, 0] = np.float32(999.0)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(0)

    out = _run(seed, [_mk_inputs(buttons=B)])[0]
    assert int(out["items"]["exists"][0]) == 0


def test_sheik_needle_mid_start_with_hidden_count_does_not_spawn_held_article_negative() -> None:
    # ftSk_SpecialNStart_Anim calls it_802B19AC only when Start has no frames remaining, then
    # immediately enters Loop. A positive stored count is not itself article publication authority.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::ftSk_SpecialNStart_Anim
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_START)
    seed["animation_index"][0, 0] = np.uint32(295)
    seed["action_frame"][0, 0] = np.int16(1)
    seed["anim_frame_f32"][0, 0] = np.float32(1.0)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(1)

    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["items"]["exists"][0]) == 0


def test_sheik_needle_end_without_hidden_count_does_not_spawn_thrown_article_negative() -> None:
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_END)
    seed["animation_index"][0, 0] = np.uint32(298)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(0)
    seed["sheik_needle_specialn_timer_u8"][0, 0] = np.uint8(2)

    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["items"]["exists"][0]) == 0


def _install_sheik_item(
    seed: np.ndarray,
    *,
    item_type: int,
    state: int,
    timer: float,
    pos_x: float = 10.0,
    pos_y: float = 20.0,
    vel_x: float = 3.0,
    vel_y: float = 0.0,
) -> None:
    seed["items"]["exists"][0, 0] = np.uint8(1)
    seed["items"]["type"][0, 0] = np.uint16(item_type)
    seed["items"]["state"][0, 0] = np.uint8(state)
    seed["items"]["owner"][0, 0] = np.int8(0)
    seed["items"]["instance_id"][0, 0] = np.uint16(17)
    seed["items"]["spawn_id"][0, 0] = np.uint32(33)
    seed["items"]["direction"][0, 0] = np.float32(1.0)
    seed["items"]["timer"][0, 0] = np.float32(timer)
    seed["items"]["pos_x"][0, 0] = np.float32(pos_x)
    seed["items"]["pos_y"][0, 0] = np.float32(pos_y)
    seed["items"]["vel_x"][0, 0] = np.float32(vel_x)
    seed["items"]["vel_y"][0, 0] = np.float32(vel_y)


def test_sheik_thrown_needle_updates_in_free_run_without_reseed_bridge() -> None:
    # Thrown Needle is a live item GObj: its item Anim/Phys/Coll callbacks run during normal
    # item phase, not only when replay reseed reconstruction is active.
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::{
    #   itSeakneedlethrown_UnkMotion0_Anim,itSeakneedlethrown_UnkMotion0_Phys}
    # refs/melee/src/melee/it/item.c::{Item_80269528,Item_802697D4}
    seed = _seed_base("sheik")
    _install_sheik_item(seed, item_type=ITEM_SHEIK_NEEDLE_THROWN, state=0, timer=2.0)

    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["items"]["exists"][0]) == 1
    assert float(out["items"]["timer"][0]) == pytest.approx(1.0)
    assert float(out["items"]["pos_x"][0]) == pytest.approx(13.0)


@pytest.mark.parametrize("state", [1, 4])
def test_sheik_thrown_needle_drop_gravity_comes_from_data_table(state: int) -> None:
    # itSeakneedlethrown_UnkMotion{1,4}_Phys: x40_vel.y += xDE0 (gravity), clamp to xDDC. Replay
    # seeds of pre-existing dropped/bounced Needles have no hidden xDE0 lane, so motion_step recovers
    # the gravity from the visible y velocity using the MSLITAR1 data tables (needle_drop_gravity /
    # needle_bounce_gravity), not a local literal array. vel_y=-1.0 is an exact multiple of the first
    # source gravity entry (-0.1), so the recovered step is vel_y -> -1.1 with no min clamp.
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::{
    #   itSeakneedlethrown_UnkMotion1_Phys,itSeakneedlethrown_UnkMotion4_Phys,it_803F6FC0,it_803F7040}
    import msl_binding

    article = msl_binding.item_article_params(7)
    grav_key = "needle_drop_gravity" if state == 1 else "needle_bounce_gravity"
    assert article[grav_key][0] == pytest.approx(-0.1)

    seed = _seed_base("sheik")
    _install_sheik_item(
        seed, item_type=ITEM_SHEIK_NEEDLE_THROWN, state=state, timer=30.0, vel_x=0.0, vel_y=-1.0
    )
    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["items"]["exists"][0]) == 1
    assert float(out["items"]["vel_y"][0]) == pytest.approx(-1.1)


def test_sheik_thrown_needle_no_body_hit_when_far_negative() -> None:
    # Negative: a state-0 Needle that overlaps no hurtbox deals no damage and stays flying (state 0).
    seed = _seed_base("sheik")
    pct0 = float(seed["percent"][0, 1])
    _install_sheik_item(
        seed, item_type=ITEM_SHEIK_NEEDLE_THROWN, state=0, timer=30.0,
        pos_x=0.0, pos_y=120.0, vel_x=4.0, vel_y=0.0,
    )
    out = _run(seed, [_mk_inputs()])[0]
    assert float(out["percent"][1]) == pytest.approx(pct0, abs=0.01)
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["state"][0]) == 0


def test_sheik_bounced_needle_does_not_body_hit_negative() -> None:
    # Negative: the state-4 bounced Needle has no active HitCapsule (state-1..4 scripts clear it), so
    # overlapping a body deals no damage.
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::ItemStateTable
    seed = _seed_base("sheik")
    pct0 = float(seed["percent"][0, 1])
    _install_sheik_item(
        seed, item_type=ITEM_SHEIK_NEEDLE_THROWN, state=4, timer=30.0,
        pos_x=0.0, pos_y=7.0, vel_x=0.0, vel_y=-1.0,
    )
    out = _run(seed, [_mk_inputs()])[0]
    assert float(out["percent"][1]) == pytest.approx(pct0, abs=0.01)


def test_sheik_thrown_needle_timer_one_destroys_before_motion() -> None:
    # States 0..3 return it_80273130 from Anim; a lifeTimer of 1 destroys the item before
    # Phys/Coll publication.
    seed = _seed_base("sheik")
    _install_sheik_item(seed, item_type=ITEM_SHEIK_NEEDLE_THROWN, state=0, timer=1.0)

    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["items"]["exists"][0]) == 0
    assert float(out["items"]["pos_x"][0]) == pytest.approx(0.0)


def test_sheik_bounced_needle_state4_anim_does_not_decrement_lifetime_negative() -> None:
    # State 4's Anim callback returns false instead of it_80273130; it still runs Phys movement.
    seed = _seed_base("sheik")
    _install_sheik_item(
        seed,
        item_type=ITEM_SHEIK_NEEDLE_THROWN,
        state=4,
        timer=1.0,
        vel_x=0.0,
        vel_y=2.0,
    )

    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["items"]["exists"][0]) == 1
    assert float(out["items"]["timer"][0]) == pytest.approx(1.0)
    assert float(out["items"]["pos_y"][0]) > 20.0


def test_sheik_bounced_needle_clears_on_generic_item_bottom_blast_bound() -> None:
    # Generic item proc order for thrown Needles is Phys/integration, then Item_802696CC blast-bound
    # destruction. Item creation enables side/bottom cleanup by default (xDCC_flag.b3=1, b4567=15),
    # so a bounced state-4 Needle crossing FD's extracted bottom blast bound is cleared before any
    # later item collision owner can preserve stale slots.
    # refs/melee/src/melee/it/item.c::{Item_80268B18,Item_802697D4,Item_802696CC}
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::{
    #   itSeakneedlethrown_UnkMotion4_Phys,it_802AFD8C}
    # data/stages/final_destination.json:blast_bounds_world.bottom
    seed = _seed_base("sheik")
    _install_sheik_item(
        seed,
        item_type=ITEM_SHEIK_NEEDLE_THROWN,
        state=4,
        timer=120.0,
        pos_x=0.0,
        pos_y=-139.0,
        vel_x=0.0,
        vel_y=-1.0,
    )

    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["items"]["exists"][0]) == 0


def test_sheik_bounced_needle_inside_blast_bound_survives_negative() -> None:
    # Adjacent negative: state 4 does not decrement its lifetime in Anim and must not be cleared while
    # still inside the generic item blast bounds after Phys/integration.
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::itSeakneedlethrown_UnkMotion4_Anim
    # refs/melee/src/melee/it/item.c::{Item_802697D4,Item_802696CC}
    seed = _seed_base("sheik")
    _install_sheik_item(
        seed,
        item_type=ITEM_SHEIK_NEEDLE_THROWN,
        state=4,
        timer=1.0,
        pos_x=0.0,
        pos_y=-130.0,
        vel_x=0.0,
        vel_y=2.0,
    )

    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["state"][0]) == 4
    assert float(out["items"]["timer"][0]) == pytest.approx(1.0)


def test_sheik_held_needle_loop_keeps_article_but_cancel_destroys() -> None:
    # Held Needle self-destructs when its SpecialN owner pointer fp->fv.sk.x4 is cleared.
    # Loop keeps that pointer live; Cancel clears it in Anim.
    # refs/melee/src/melee/it/items/itseakneedleheld.c::itSeakneedleheld_UnkMotion0_Anim
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    #   ftSk_SpecialNLoop_Anim,ftSk_SpecialNCancel_Anim}
    loop_seed = _seed_base("sheik")
    loop_seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_LOOP)
    loop_seed["animation_index"][0, 0] = np.uint32(296)
    _install_sheik_item(loop_seed, item_type=ITEM_SHEIK_NEEDLE_HELD, state=0, timer=1400.0)
    loop_out = _run(loop_seed, [_mk_inputs(buttons=B)])[0]
    assert int(loop_out["items"]["exists"][0]) == 1
    assert int(loop_out["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_HELD

    cancel_seed = _seed_base("sheik")
    cancel_seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_CANCEL)
    cancel_seed["animation_index"][0, 0] = np.uint32(297)
    _install_sheik_item(cancel_seed, item_type=ITEM_SHEIK_NEEDLE_HELD, state=0, timer=1400.0)
    cancel_out = _run(cancel_seed, [_mk_inputs()])[0]
    assert int(cancel_out["items"]["exists"][0]) == 0


def test_sheik_held_needle_common_action_keeps_replay_visible_owner_pointer() -> None:
    # Replay-visible held Needle articles carry the item-local seakneedleheld.owner pointer even when
    # the fighter has already left SpecialN. Item Anim only destroys when that internal pointer is
    # NULL; common states do not imply that clear. Explicit owners such as Cancel_Anim / shootNeedles
    # clear the pointer or slot separately.
    # refs/melee/src/melee/it/items/itseakneedleheld.c::itSeakneedleheld_UnkMotion0_Anim
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    #   ftSk_SpecialNCancel_Anim,ftSk_SpecialNEnd_Anim,shootNeedles}
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_LANDING)
    _install_sheik_item(seed, item_type=ITEM_SHEIK_NEEDLE_HELD, state=0, timer=1400.0)

    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_HELD


def test_sheik_held_needle_end_non_latch_keeps_until_shoot_needles_owner() -> None:
    # Item Anim runs before SpecialNEnd_Anim. A visible held article on an End non-latch frame keeps
    # its item-local owner pointer through the pre-fighter item phase; only the later shootNeedles
    # accessory/item owner clears the held slot on latch frames 2/5/8/11/14/17.
    # refs/melee/src/melee/it/items/itseakneedleheld.c::itSeakneedleheld_UnkMotion0_Anim
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{ftSk_SpecialNEnd_Anim,shootNeedles}
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_END)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(2)
    seed["sheik_needle_specialn_timer_u8"][0, 0] = np.uint8(1)
    _install_sheik_item(seed, item_type=ITEM_SHEIK_NEEDLE_HELD, state=0, timer=1400.0)

    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_HELD


def _debug_sheik_needle_damage_callback(seed: np.ndarray) -> tuple[np.void, int]:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.debug_refresh_combat_geometry(handle)
        msl_binding.debug_clear_hitboxes_world(handle, 0, 1)
        msl_binding.debug_set_hitbox_world(handle, 0, 1, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 1, 0, int(HIT_GROUNDED | HIT_AERIAL))
        msl_binding.debug_set_hitbox_group(handle, 0, 1, 0, 0)
        msl_binding.debug_set_hitbox_kb_params(handle, 0, 1, 0, 90, 100, 0, 20)
        msl_binding.debug_clear_hurtcaps_world(handle, 0, 0)
        msl_binding.debug_set_hurtcap_world(handle, 0, 0, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)
        msl_binding.debug_set_hurtcap_height(handle, 0, 0, 0, 1)
        msl_binding.debug_combat_resolve(handle)
        needle_count = int(msl_binding.debug_get_sheik_needle_count(handle, 0, 0))
        msl_binding.write_compare(handle, out_bytes)
    finally:
        msl_binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy(), needle_count


def test_sheik_needle_damage_callback_drops_live_held_stock() -> None:
    import msl_binding

    # ftCommon_8007DB58 calls Sheik's take_dmg callback before Damage entry clears callback
    # pointers. With fp->fv.sk.x4 live, ftSk_SpecialN_80111FBC converts every stored Needle into
    # dropped thrown-Needle state 1 articles and clears fv.sk.x0.
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DB58
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::ftSk_SpecialN_80111FBC
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::{it_802AFD8C,it_802B00F4}
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_LOOP)
    seed["animation_index"][0, 0] = np.uint32(296)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(3)
    seed["pos_x"][0, 1] = np.float32(0.0)
    _install_sheik_item(seed, item_type=ITEM_SHEIK_NEEDLE_HELD, state=0, timer=1400.0)

    out, needle_count = _debug_sheik_needle_damage_callback(seed)
    assert ACT_DAMAGE_N_1 <= int(out["action_id"][0]) <= ACT_DAMAGE_N_3
    assert needle_count == 0
    assert [int(out["items"]["type"][i]) for i in range(3)] == [ITEM_SHEIK_NEEDLE_THROWN] * 3
    assert [int(out["items"]["state"][i]) for i in range(3)] == [1, 1, 1]
    article = msl_binding.item_article_params(7)
    assert [float(out["items"]["timer"][i]) for i in range(3)] == pytest.approx(
        [float(article["needle_lifetime_frames"])] * 3
    )
    sheik_attrs = json.loads((ROOT / "data" / "characters" / "sheik.json").read_text())
    assert float(out["items"]["pos_x"][0]) == pytest.approx(0.0)
    assert float(out["items"]["pos_y"][0]) == pytest.approx(
        float(sheik_attrs["sheik_needle_ground_spawn_y_offset"])
    )


def test_sheik_needle_damage_callback_cancel_gap_clears_without_drop_negative() -> None:
    # In SpecialNCancel the take_dmg callback is still installed, but Cancel_Anim has already nulled
    # fp->fv.sk.x4 (the held Needle is destroyed). Damage in that state clears fv.sk.x0 without
    # spawning dropped Needles.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{ftSk_SpecialNCancel_Anim,setDmgCallbacks}
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_N_CANCEL)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(3)
    seed["pos_x"][0, 1] = np.float32(0.0)

    out, needle_count = _debug_sheik_needle_damage_callback(seed)
    assert ACT_DAMAGE_N_1 <= int(out["action_id"][0]) <= ACT_DAMAGE_N_3
    assert needle_count == 0
    assert int(out["items"]["exists"][0]) == 0


def test_sheik_needle_damage_callback_common_action_does_not_install_callback_negative() -> None:
    # A common action like Wait does not install ftSk_Init_80110198 (it is installed by SpecialN and
    # by SpecialS while a live Chain article exists). With the callback uninstalled, taking damage
    # with a stored count and a (hypothetically) live held Needle neither drops Needles nor clears
    # the held article.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_Init.c::ftSk_Init_80110198
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(3)
    seed["pos_x"][0, 1] = np.float32(0.0)
    _install_sheik_item(seed, item_type=ITEM_SHEIK_NEEDLE_HELD, state=0, timer=1400.0)

    out, needle_count = _debug_sheik_needle_damage_callback(seed)
    assert ACT_DAMAGE_N_1 <= int(out["action_id"][0]) <= ACT_DAMAGE_N_3
    # Callback not installed in Wait: stored count survives and the held Needle article is untouched.
    assert needle_count == 3
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_HELD
    thrown = [
        i for i in range(3) if int(out["items"]["type"][i]) == ITEM_SHEIK_NEEDLE_THROWN
    ]
    assert thrown == []


def test_sheik_needle_damage_callback_specials_with_chain_clears_without_drop() -> None:
    # ftSk_Init_80110198 is also installed during SpecialS while a live Chain article exists
    # (fv.sk.x8 != NULL). Its Needle half clears fv.sk.x0; the held Needle pointer fv.sk.x4 is null in
    # SpecialS, so the stored count clears without dropping any Needles.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_Init.c::ftSk_Init_80110198
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c (take_dmg_cb on fv.sk.x8 != NULL)
    ACT_SK_SPECIAL_S_ACTIVE = 350
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_S_ACTIVE)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(3)
    seed["pos_x"][0, 1] = np.float32(0.0)
    # Live Chain article owned by the Sheik player installs the callback during SpecialS.
    _install_sheik_item(seed, item_type=ITEM_SHEIK_CHAIN, state=1, timer=1400.0)

    out, needle_count = _debug_sheik_needle_damage_callback(seed)
    assert ACT_DAMAGE_N_1 <= int(out["action_id"][0]) <= ACT_DAMAGE_N_3
    assert needle_count == 0
    thrown = [i for i in range(3) if int(out["items"]["type"][i]) == ITEM_SHEIK_NEEDLE_THROWN]
    assert thrown == []


def test_sheik_needle_damage_callback_specials_without_chain_does_not_fire() -> None:
    # In SpecialS with no live Chain article, ftSk_SpecialS never installs ftSk_Init_80110198, so a
    # hit does not clear the stored Needle count.
    ACT_SK_SPECIAL_S_ACTIVE = 350
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_S_ACTIVE)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(3)
    seed["pos_x"][0, 1] = np.float32(0.0)

    out, needle_count = _debug_sheik_needle_damage_callback(seed)
    assert ACT_DAMAGE_N_1 <= int(out["action_id"][0]) <= ACT_DAMAGE_N_3
    assert needle_count == 3


def test_sheik_needle_seed_lane_clears_on_visible_damage_action() -> None:
    import msl_binding

    # Damage after a SpecialN state clears the count (callback was installed); damage after a common
    # action does not (no installed callback), so a stored count survives a non-special hit.
    char = np.full((3, 1), 7, dtype=np.uint8)
    no_chain = np.zeros((3, 1), dtype=np.uint8)
    action = np.array([[ACT_SK_SPECIAL_N_LOOP], [ACT_DAMAGE_N_1], [ACT_WAIT]], dtype=np.uint16)
    frame = np.array([[0], [0], [0]], dtype=np.int16)

    count, timer = msl_binding.derive_sheik_needle_seed_lanes(char, action, frame, no_chain, 7)
    assert [int(v) for v in count[:, 0]] == [1, 0, 0]
    assert [int(v) for v in timer[:, 0]] == [0, 0, 0]

    # prev_action is Wait (no installed callback): the count built during the Loop survives the hit.
    action_common = np.array(
        [[ACT_SK_SPECIAL_N_LOOP], [ACT_WAIT], [ACT_DAMAGE_N_1]], dtype=np.uint16
    )
    count2, _ = msl_binding.derive_sheik_needle_seed_lanes(char, action_common, frame, no_chain, 7)
    assert [int(v) for v in count2[:, 0]] == [1, 1, 1]


def test_sheik_needle_seed_lane_chain_callback_installed_clears_count() -> None:
    import msl_binding

    # ftSk_SpecialS installs the same ftSk_Init_80110198 take_dmg/death callback as SpecialN, but
    # only while a live Chain article exists (fv.sk.x8). So damage during SpecialS clears the stored
    # Needle count iff the Chain-article-present lane is set on the SpecialS row.
    ACT_SK_SPECIAL_S_ACTIVE = 350
    char = np.full((3, 1), 7, dtype=np.uint8)
    action = np.array(
        [[ACT_SK_SPECIAL_N_LOOP], [ACT_SK_SPECIAL_S_ACTIVE], [ACT_DAMAGE_N_1]], dtype=np.uint16
    )
    frame = np.array([[0], [0], [0]], dtype=np.int16)

    # Chain article present on the SpecialS row -> callback installed -> count clears on the hit.
    chain_present = np.array([[0], [1], [1]], dtype=np.uint8)
    count, _ = msl_binding.derive_sheik_needle_seed_lanes(char, action, frame, chain_present, 7)
    assert [int(v) for v in count[:, 0]] == [1, 1, 0]

    # No live Chain article -> SpecialS did not install the callback -> count survives the hit.
    no_chain = np.zeros((3, 1), dtype=np.uint8)
    count2, _ = msl_binding.derive_sheik_needle_seed_lanes(char, action, frame, no_chain, 7)
    assert [int(v) for v in count2[:, 0]] == [1, 1, 1]


@pytest.mark.parametrize(
    ("action", "x0", "start_state", "expected_state"),
    [
        (ACT_SK_SPECIAL_S_START, 22, 0, 1),
        (ACT_SK_SPECIAL_AIR_S_START, 31, 1, 3),
        (ACT_SK_SPECIAL_S_END, 17, 3, 4),
        (ACT_SK_SPECIAL_AIR_S_END, 26, 4, 4),
    ],
)
def test_sheik_chain_article_state_thresholds_follow_source_timer(
    action: int,
    x0: int,
    start_state: int,
    expected_state: int,
) -> None:
    # Sheik Chain article state transitions are owned by ftSk_SpecialS.c and itseakchain.c, using
    # mv.sk.specials.x0 thresholds rather than replay row ids or final action labels.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialS_CheckInitChain,ftSk_SpecialSEnd_Anim,ftSk_SpecialAirSEnd_Anim}
    # refs/melee/src/melee/it/items/itseakchain.c::{
    #   it_802BCFC4,it_802BCED4,it_802BCF84,it_802BB20C}
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(action)
    seed["sheik_chain_x0_u8"][0, 0] = np.uint8(x0)
    _install_sheik_item(seed, item_type=ITEM_SHEIK_CHAIN, state=start_state, timer=1400.0)

    out = _run(seed, [_mk_inputs(buttons=B)])[0]
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["state"][0]) == expected_state


def test_sheik_chain_article_destroy_threshold_and_orphan_owner_negative() -> None:
    seed = _seed_base("sheik")
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_S_END)
    seed["sheik_chain_x0_u8"][0, 0] = np.uint8(27)
    _install_sheik_item(seed, item_type=ITEM_SHEIK_CHAIN, state=0, timer=1400.0)
    out = _run(seed, [_mk_inputs()])[0]
    assert int(out["items"]["exists"][0]) == 0

    orphan_seed = _seed_base("sheik")
    orphan_seed["action_id"][0, 0] = np.uint16(ACT_WAIT)
    _install_sheik_item(orphan_seed, item_type=ITEM_SHEIK_CHAIN, state=3, timer=1400.0)
    orphan_out = _run(orphan_seed, [_mk_inputs()])[0]
    assert int(orphan_out["items"]["exists"][0]) == 0


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "seed_state", "ref_state"),
    [
        (249, 0, 1),
        (258, 1, 3),
        (484, 3, 4),
        (1392, 0, 1),
        (1401, 1, 3),
        (1431, 3, 4),
    ],
)
def test_sheik_demo_chain_article_state_machine_replay_real(
    record: int,
    seed_state: int,
    ref_state: int,
) -> None:
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["items"]["type"][0]) == ITEM_SHEIK_CHAIN
    assert int(seed["items"]["state"][0]) == seed_state
    assert int(ref["items"]["state"][0]) == ref_state

    out = _run_sample_row(samples, record)
    assert int(out["items"]["exists"][0]) == int(ref["items"]["exists"][0])
    assert int(out["items"]["type"][0]) == ITEM_SHEIK_CHAIN
    assert int(out["items"]["state"][0]) == ref_state


@pytest.mark.integration
def test_sheik_demo2_chain_end_pre_destroy_frame_preserves_retract_state_replay_real() -> None:
    # ftSk_SpecialS{Air}End_Anim calls it_802BCF84 at x24 and it_802BB20C at x28. A state-4
    # one-step seed at x0=26 must not be collapsed to state 0 by the fighter timer; state 0 is owned
    # by the item retract helper it_802BC94C -> it_2725_Logic54_PickedUp when hidden ItemLink
    # frontier geometry reaches the hand, which is not the same source predicate as x28-1.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialSEnd_Anim,ftSk_SpecialAirSEnd_Anim}
    # refs/melee/src/melee/it/items/itseakchain.c::{
    #   it_802BCF84,it_802BB20C,it_802BC94C,it_2725_Logic54_PickedUp}
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game_2.msl")
    seed = samples[7532]["seed_t"]
    ref = samples[7532]["ref_t1"]
    assert int(seed["action_id"][0]) == ACT_SK_SPECIAL_S_END
    assert int(seed["sheik_chain_x0_u8"][0]) == 26
    assert int(seed["items"]["type"][0]) == ITEM_SHEIK_CHAIN
    assert int(seed["items"]["state"][0]) == 4
    assert int(ref["items"]["state"][0]) == 4

    out = _run_sample_row(samples, 7532)
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["type"][0]) == ITEM_SHEIK_CHAIN
    assert int(out["items"]["state"][0]) == 4


@pytest.mark.integration
@pytest.mark.parametrize("record", [2743, 3577, 4024])
def test_sheik_demo_needle_cancel_destroys_held_article_replay_real(record: int) -> None:
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][0]) in (ACT_SK_SPECIAL_N_CANCEL, ACT_SK_SPECIAL_AIR_N_CANCEL)
    assert int(seed["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_HELD
    assert int(ref["items"]["exists"][0]) == 0

    out = _run_sample_row(samples, record)
    assert int(out["items"]["exists"][0]) == 0


@pytest.mark.integration
@pytest.mark.parametrize("record", [2871, 3226, 5095])
def test_sheik_demo_thrown_needle_lifetime_expires_replay_real(record: int) -> None:
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["items"]["type"][0]) == ITEM_SHEIK_NEEDLE_THROWN
    assert int(seed["items"]["state"][0]) in (0, 1, 2, 3)
    assert float(seed["items"]["timer"][0]) <= 1.0
    assert int(ref["items"]["exists"][0]) == 0

    out = _run_sample_row(samples, record)
    assert int(out["items"]["exists"][0]) == 0


@pytest.mark.integration
def test_sheik_needle_loop_analog_trigger_edge_cancel_replay_real_lock() -> None:
    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/RuralReasonableRat.msl")
    record = 2644
    assert int(samples[record]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_N_LOOP
    assert int(samples[record]["ref_t1"]["action_id"][0]) == ACT_SK_SPECIAL_N_CANCEL
    assert int(samples[record]["prev_input_t"]["p"]["buttons"][0]) & B
    assert int(samples[record]["input_t"]["p"]["buttons"][0]) & B
    assert int(samples[record]["prev_input_t"]["p"]["r"][0]) <= 0
    assert int(samples[record]["input_t"]["p"]["r"][0]) > 0

    out = _run_sample_row(samples, record)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_N_CANCEL


def _seed_needle_over_fox_defender(*, defender_x: float = 20.0) -> np.ndarray:
    # Sheik (P0) owns a state-0 flying Needle positioned on Fox (P1)'s body so the swept Needle
    # HitCapsule overlaps P1's hurtcaps -> BODY damage, unless an active ReflectDesc or ShieldDesc
    # defers it. The default _seed_base places P1 far away; bring P1 under the Needle.
    seed = _seed_base("sheik")
    seed["pos_x"][0, 1] = np.float32(defender_x)
    seed["pos_y"][0, 1] = np.float32(0.0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["shield_hp"][0, 1] = np.float32(60.0)
    _install_sheik_item(
        seed, item_type=ITEM_SHEIK_NEEDLE_THROWN, state=0, timer=30.0,
        pos_x=defender_x, pos_y=8.0, vel_x=0.2, vel_y=0.0,
    )
    return seed


def _inputs_defender_holds_shield() -> np.ndarray:
    inp = np.zeros((1,), dtype=INPUT_DTYPE)
    inp["p"]["l"][0, 1] = np.uint8(140)  # full analog shield trigger on the defender (P1)
    return inp.view(np.uint8).reshape((1, INPUT_DTYPE.itemsize))


def test_sheik_thrown_needle_body_hits_unguarded_defender_positive() -> None:
    # Positive control: a state-0 flying Needle overlapping an UNGUARDED defender's body deals the
    # 3-damage Needle BODY hit. This pins the synthetic overlap geometry so the ShieldDesc negatives
    # below are not vacuous (the same seed body-hits when no shield is up).
    seed = _seed_needle_over_fox_defender()
    pct0 = float(seed["percent"][0, 1])
    out = _run(seed, [_mk_inputs()])[0]
    assert float(out["percent"][1]) == pytest.approx(pct0 + 3.0, abs=0.01)


def test_sheik_thrown_needle_dmgdealt_bounce_destroy_under_synthetic_rng() -> None:
    # it_2725_Logic109_DmgDealt post-hit fate under the sim's DETERMINISTIC synthetic gameplay RNG.
    # The thrown-Needle BODY hit runs HSD_Randi(3): ==0 -> bounce (item_state 4, lifeTimer from
    # attr->x4 == needle_bounce_lifetime_frames, horizontal drift vel_x = SetupBounce xDD8 =
    # needle_bounce_x_vel[Randi(8)] signed by Randi(2), visible y velocity ABS(it_803F7020[Randi(8)])
    # drawn from the MSLITAR1 needle_bounce_min_vel_y table); else -> destroy (item cleared). The
    # 3-damage BODY hit lands in BOTH outcomes.
    #
    # The replay-EXACT bounce-vs-destroy choice is owned by HSD visual particle-generator draws the
    # headless sim intentionally does not model: the RNGFRAME trace proves the Needle Randi(3) reads a
    # seed past a per-frame particle storm whose 65536-multiple gap scrambles exactly the high bits
    # Randi(3) consumes (>>16). So this locks the post-hit-fate STRUCTURE + behavior under the synthetic
    # stream -- NOT replay byte parity, which is documented out-of-scope (no fixed/row-local offset).
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::{it_2725_Logic109_DmgDealt,
    #   itSeakNeedleThrown_SetupBounce}
    import msl_binding

    article = msl_binding.item_article_params(7)
    bounce_vels = [abs(float(v)) for v in article["needle_bounce_min_vel_y"]]
    bounce_x_vels = [abs(float(v)) for v in article["needle_bounce_x_vel"]]
    bounce_lifetime = float(article["needle_bounce_lifetime_frames"])

    saw_bounce = False
    saw_destroy = False
    for hw in range(0, 24):
        seed = _seed_needle_over_fox_defender()
        seed["frame_pre_random_seed"][0] = np.uint32(hw << 16)
        pct0 = float(seed["percent"][0, 1])
        out = _run(seed, [_mk_inputs()])[0]
        # BODY damage lands regardless of the post-hit fate.
        assert float(out["percent"][1]) == pytest.approx(pct0 + 3.0, abs=0.01)
        if int(out["items"]["exists"][0]) == 0:
            saw_destroy = True
            continue
        # Survivor is a bounced (state 4) Needle with data-table-owned upward velocity + horizontal drift.
        assert int(out["items"]["state"][0]) == 4
        assert any(
            abs(float(out["items"]["vel_x"][0])) == pytest.approx(v) for v in bounce_x_vels
        ), float(out["items"]["vel_x"][0])
        assert float(out["items"]["timer"][0]) == pytest.approx(bounce_lifetime)
        assert any(
            float(out["items"]["vel_y"][0]) == pytest.approx(v) for v in bounce_vels
        ), float(out["items"]["vel_y"][0])
        saw_bounce = True
    assert saw_bounce, "no synthetic seed produced a Needle bounce (state 4)"
    assert saw_destroy, "no synthetic seed produced a Needle destroy"


def test_sheik_thrown_needle_dmgdealt_fate_is_deterministic_in_synthetic_rng() -> None:
    # The post-hit fate is a pure function of the deterministic synthetic gameplay RNG stream: the same
    # synthetic seed yields an identical bounce/destroy outcome and bounce velocity across runs (no
    # hidden nondeterminism). This is what "correct given the synthetic stream" means once replay-exact
    # parity is conceded to the unmodelled particle RNG.
    for hw in (0, 2):
        outcomes = []
        for _ in range(2):
            seed = _seed_needle_over_fox_defender()
            seed["frame_pre_random_seed"][0] = np.uint32(hw << 16)
            out = _run(seed, [_mk_inputs()])[0]
            outcomes.append(
                (
                    int(out["items"]["exists"][0]),
                    int(out["items"]["state"][0]),
                    round(float(out["items"]["vel_y"][0]), 4),
                )
            )
        assert outcomes[0] == outcomes[1]


def _seed_needle_over_shielding_fox_defender(*, needle_x: float = 20.0, needle_y: float = 8.0):
    # Active-ShieldDesc defender (Guard + 221B_b0) with the Needle positioned over the shield bubble.
    seed = _seed_needle_over_fox_defender()
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD)
    seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)
    seed["state_flags"][0, 1, 2] = np.uint8(0x80)  # 221B_b0 ShieldDesc active
    seed["items"]["pos_x"][0, 0] = np.float32(needle_x)
    seed["items"]["pos_y"][0, 0] = np.float32(needle_y)
    return seed


def test_sheik_thrown_needle_hits_active_shielddesc_positive() -> None:
    # Positive Needle-vs-ShieldDesc contact (source order ReflectDesc -> ShieldDesc -> BODY): a defender
    # holding an active ShieldDesc resolves the Needle HitCapsule against the shield bubble before the
    # BODY hurtcaps. The shield ABSORBS the BODY (no fighter percent damage), takes the Needle shield
    # damage (shield_hp drops well below the hold-only decay), and the Needle runs it_2725_Logic109_
    # HitShield -- HSD_Randi(3)==0 bounce (state 4) else destroy. The bounce/destroy CHOICE is the same
    # particle-RNG-owned/out-of-scope fate as DmgDealt, so this scans the synthetic RNG and only requires
    # that the Needle HitShielded (NOT the stale fly-through state-0), that both branches are reachable,
    # and that the shield took damage in every case.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688,ftColl_80076CBC}
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_HitShield
    hold_only_hp = float(
        _run(_seed_needle_over_shielding_fox_defender(needle_x=200.0, needle_y=200.0),
             [_inputs_defender_holds_shield()])[0]["shield_hp"][1]
    )
    saw_bounce = False
    saw_destroy = False
    for hw in range(0, 16):
        seed = _seed_needle_over_shielding_fox_defender()
        seed["frame_pre_random_seed"][0] = np.uint32(hw << 16)
        pct0 = float(seed["percent"][0, 1])
        out = _run(seed, [_inputs_defender_holds_shield()])[0]
        # Shield absorbs the BODY: no fighter percent damage.
        assert float(out["percent"][1]) == pytest.approx(pct0, abs=0.01)
        # Shield took the Needle shield damage (clearly past the shield-hold decay floor).
        assert float(out["shield_hp"][1]) < hold_only_hp - 1.0
        # Needle HitShielded: bounced (state 4, survives) or destroyed (cleared) -- never fly-through.
        if int(out["items"]["exists"][0]) == 0:
            saw_destroy = True
            continue
        assert int(out["items"]["state"][0]) == 4
        saw_bounce = True
    assert saw_bounce, "no synthetic seed produced a Needle shield bounce (state 4)"
    assert saw_destroy, "no synthetic seed produced a Needle shield destroy"


def test_sheik_thrown_needle_misses_shield_bubble_flies_past_negative() -> None:
    # Adjacent no-contact negative: with the SAME active ShieldDesc but the Needle positioned OUTSIDE the
    # shield bubble, there is no HitShield contact -- the Needle travels past unchanged (state 0,
    # survives), the shield takes NO Needle damage (shield_hp matches the hold-only decay), and the
    # fighter takes no BODY damage. This keeps the positive above non-vacuous (the contact is geometry-
    # gated, not "any active shield bounces the Needle").
    hold_only_hp = float(
        _run(_seed_needle_over_shielding_fox_defender(needle_x=200.0, needle_y=200.0),
             [_inputs_defender_holds_shield()])[0]["shield_hp"][1]
    )
    seed = _seed_needle_over_shielding_fox_defender(needle_x=20.0, needle_y=120.0)
    pct0 = float(seed["percent"][0, 1])
    out = _run(seed, [_inputs_defender_holds_shield()])[0]
    assert float(out["percent"][1]) == pytest.approx(pct0, abs=0.01)
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["state"][0]) == 0  # Needle missed the shield bubble: no HitShield
    assert float(out["shield_hp"][1]) == pytest.approx(hold_only_hp, abs=0.01)  # no Needle shield dmg


def _seed_needle_over_reflecting_fox_defender(*, needle_x: float = 20.0, needle_y: float = 8.0):
    # Reflect-only defender: GuardOn pose (the GuardReflect submotion ftCo_MS_GuardReflect uses the
    # GuardOn row, so item_guard_reflect_center_xyz resolves a real pose shield-bone center) + 2218
    # REFLECTING set + 221B_b0 ShieldDesc cleared. The Needle starts over the reflect bubble.
    seed = _seed_needle_over_fox_defender()
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_ON)
    seed["state_flags"][0, 1, 0] = np.uint8(0x10)  # fp+0x2218 REFLECTING
    seed["state_flags"][0, 1, 2] = np.uint8(0x00)  # 221B_b0 ShieldDesc cleared (reflect-only)
    seed["items"]["pos_x"][0, 0] = np.float32(needle_x)
    seed["items"]["pos_y"][0, 0] = np.float32(needle_y)
    seed["items"]["vel_x"][0, 0] = np.float32(0.2)
    seed["items"]["vel_y"][0, 0] = np.float32(0.0)
    seed["items"]["direction"][0, 0] = np.float32(1.0)
    return seed


def test_sheik_thrown_needle_reflected_by_reflectdesc_positive() -> None:
    # Positive Needle-vs-ReflectDesc (source order ReflectDesc -> ShieldDesc -> BODY): an active
    # reflector resolves the Needle HitCapsule against the ReflectDesc bubble before ShieldDesc/BODY.
    # ftColl_80077464 transfers the article to the reflector; it_2725_Logic109_Reflected (no RNG)
    # reverses the constant-speed state-0 Needle (vel -> -vel), flips facing, and halves the life
    # (lifeTimer = halfLifeTimer = spawn_life * ItemCommonData::x4C_float = 0.5). No BODY damage and no
    # shield damage. The reflect center comes from the same pose/part path the runtime uses
    # (item_guard_reflect_center_xyz over the GuardOn shield-bone), not a constant.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077464}
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_Reflected
    import msl_binding

    half_life = float(msl_binding.item_article_params(7)["needle_lifetime_frames"]) * 0.5
    seed = _seed_needle_over_reflecting_fox_defender(needle_y=8.0)
    seed["items"]["timer"][0, 0] = np.float32(30.0)
    pct0 = float(seed["percent"][0, 1])
    shp0 = float(seed["shield_hp"][0, 1])
    out = _run(seed, [_inputs_defender_holds_shield()])[0]
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["owner"][0]) == 1  # ownership transferred to the reflector
    assert float(out["items"]["vel_x"][0]) == pytest.approx(-0.2, abs=1e-4)  # velocity reversed
    assert float(out["items"]["direction"][0]) == pytest.approx(-1.0)  # facing flipped
    assert float(out["items"]["timer"][0]) == pytest.approx(half_life, abs=0.01)  # life halved
    assert int(out["items"]["state"][0]) == 0  # not bounced/destroyed -- reflected, survives
    assert float(out["percent"][1]) == pytest.approx(pct0, abs=0.01)  # no BODY damage
    # No Needle shield damage (reflect owns the contact before ShieldDesc): only the GuardOn hold decay.
    assert float(out["shield_hp"][1]) >= shp0 - 0.2


def test_sheik_thrown_needle_misses_reflect_bubble_flies_past_negative() -> None:
    # Adjacent no-contact negative: SAME active reflector pose, but the Needle is positioned OUTSIDE the
    # ReflectDesc bubble -> no reflect contact. Ownership is NOT transferred, velocity is unchanged, and
    # there is no BODY/shield damage. This keeps the positive non-vacuous (the reflect is geometry-gated
    # by the real pose center/descriptor radius, not "any 2218 bit reverses the Needle").
    seed = _seed_needle_over_reflecting_fox_defender(needle_y=120.0)
    pct0 = float(seed["percent"][0, 1])
    out = _run(seed, [_inputs_defender_holds_shield()])[0]
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["owner"][0]) == 0  # no reflect: still owned by the thrower
    assert float(out["items"]["vel_x"][0]) == pytest.approx(0.2, abs=1e-4)  # velocity unchanged
    assert float(out["percent"][1]) == pytest.approx(pct0, abs=0.01)


def test_sheik_thrown_needle_reflect_bit_without_pose_center_falls_through_to_body() -> None:
    # Reflect MISS fallthrough (source order ReflectDesc -> ShieldDesc -> clank -> BODY): the bare
    # fp+0x2218 REFLECTING bit on a NON-guard action exposes no ReflectDesc pose center
    # (item_guard_reflect_center_xyz returns 0 outside GUARD/GUARD_ON/GUARD_REFLECT), so the Needle
    # cannot reflect. ftColl_8007925C only skips ShieldDesc/BODY on a reflect HIT (lbColl_80007BCC
    # reflect_hit overlap); a miss falls through. With 221B cleared and no shield, the Needle therefore
    # takes the ordinary BODY hit (3 dmg) and bounces/destroys -- the reflect bit alone does NOT
    # suppress BODY without a ReflectDesc overlap.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    seed = _seed_needle_over_fox_defender()
    seed["state_flags"][0, 1, 0] = np.uint8(0x10)  # fp+0x2218 REFLECTING; 221B byte left cleared
    pct0 = float(seed["percent"][0, 1])
    out = _run(seed, [_mk_inputs()])[0]  # no shield input -> shield_radius stays 0
    assert float(out["percent"][1]) == pytest.approx(pct0 + 3.0, abs=0.01)  # BODY hit (fell through)
    assert int(out["items"]["owner"][0]) == 0  # not reflected (no pose center)
    assert int(out["items"]["exists"][0]) == 0 or int(out["items"]["state"][0]) == 4  # bounced/destroyed


def test_sheik_thrown_needle_reflect_miss_falls_through_to_shielddesc() -> None:
    # Reviewer control: a defender with BOTH fp+0x2218 REFLECTING and an active ShieldDesc, where the
    # ReflectDesc bubble MISSES but the ShieldDesc bubble OVERLAPS. ftColl_8007925C only skips later
    # owners (continue) on a reflect HIT, so the reflect miss FALLS THROUGH to ShieldDesc, which
    # HitShields (shield damage + bounce/destroy). The reflect bit does NOT suppress ShieldDesc on a
    # reflect miss. Geometry: the ReflectDesc bubble sits on the guard shield-bone (~y9); a Needle below
    # it (y=0) misses reflect but the larger shield bubble still covers the body. Contrast: inside the
    # reflect bubble (y=8) ReflectDesc wins (source-prior).
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
    def _run_combined(ndl_y: float):
        seed = _seed_needle_over_fox_defender()
        seed["action_id"][0, 1] = np.uint16(ACT_GUARD_ON)
        seed["state_flags"][0, 1, 0] = np.uint8(0x10)  # fp+0x2218 REFLECTING
        seed["state_flags"][0, 1, 2] = np.uint8(0x80)  # fp+0x221B_b0 ShieldDesc active
        seed["items"]["pos_x"][0, 0] = np.float32(20.0)
        seed["items"]["pos_y"][0, 0] = np.float32(ndl_y)
        seed["items"]["vel_x"][0, 0] = np.float32(0.2)
        shp0 = float(seed["shield_hp"][0, 1])
        pct0 = float(seed["percent"][0, 1])
        return _run(seed, [_inputs_defender_holds_shield()])[0], shp0, pct0

    # Reflect MISS (below the reflect bubble) -> ShieldDesc HitShields, NOT suppressed by the reflect bit.
    out, shp0, pct0 = _run_combined(0.0)
    assert int(out["items"]["owner"][0]) == 0  # not reflected
    assert int(out["items"]["exists"][0]) == 0 or int(out["items"]["state"][0]) == 4  # HitShield fate
    assert float(out["shield_hp"][1]) < shp0 - 1.0  # shield took the Needle shield damage
    assert float(out["percent"][1]) == pytest.approx(pct0, abs=0.01)  # no BODY damage
    # Reflect HIT (inside the reflect bubble) -> ReflectDesc wins (source-prior to ShieldDesc).
    out2, _, _ = _run_combined(8.0)
    assert int(out2["items"]["owner"][0]) == 1  # reflected


# Fighter attack-HitCapsule flags used to construct a source-shaped clank setup.
_HITBOX_FLAG_ITEM_HIT_INTERACTION = 1 << 11  # MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION
_HITBOX_FLAG_CLANK = 1 << 14  # MSL_HITBOX_FLAG_CLANK


def _run_needle_item_collision(
    seed: np.ndarray,
    *,
    clank_hitbox: bool = False,
    overlap: bool = True,
    flags: int = _HITBOX_FLAG_CLANK | _HITBOX_FLAG_ITEM_HIT_INTERACTION,
    element: int = 0,  # MSL_HIT_ELEMENT_NORMAL
    clear_hurtcaps: bool = True,
    override_hurtcap_under_needle: bool = False,
) -> np.void:
    # Run ONLY items_update_collision_phase (the Needle update/collision) on a reseeded handle, so a
    # defender attack HitCapsule can be injected with real source flags (a full step re-derives/clears
    # hitboxes from the action script, so the clank must be set on the item-collision phase directly).
    # debug_set_hitbox_* uses the same hitbox lanes the runtime reads; geometry/flags are not faked.
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    comp = int(sizes["compare"])
    out_bytes = np.zeros((1, comp), dtype=np.uint8)
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        msl_binding.debug_refresh_combat_geometry(handle)
        msl_binding.debug_clear_hitboxes_world(handle, 0, 1)
        if clear_hurtcaps:
            msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
        if override_hurtcap_under_needle:
            nx = float(seed["items"]["pos_x"][0, 0])
            ny = float(seed["items"]["pos_y"][0, 0])
            msl_binding.debug_set_hurtcap_world(
                handle, 0, 1, 0, nx - 0.25, ny, 0.0, nx + 0.25, ny, 0.0, 0.5
            )
            msl_binding.debug_set_hurtcap_height(handle, 0, 1, 0, 1)
        if clank_hitbox:
            # Co-locate the fighter attack HitCapsule with the Needle (overlap) or place it far away.
            nx = float(seed["items"]["pos_x"][0, 0])
            ny = float(seed["items"]["pos_y"][0, 0])
            hx = nx if overlap else nx + 200.0
            msl_binding.debug_set_hitbox_world(handle, 0, 1, 0, hx, ny, 0.0, 2.0, 5.0, 1)
            msl_binding.debug_set_hitbox_flags(handle, 0, 1, 0, int(flags))
            msl_binding.debug_set_hitbox_element(handle, 0, 1, 0, int(element))
            msl_binding.debug_set_hitbox_kb_params(handle, 0, 1, 0, 90, 100, 0, 20)
        msl_binding.debug_run_item_collision_phase(handle)
        msl_binding.write_compare(handle, out_bytes)
    finally:
        msl_binding.destroy(handle)
    return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()


def _clank_seed(rng_hw: int = 0) -> np.ndarray:
    seed = _seed_needle_over_fox_defender()
    seed["frame_pre_random_seed"][0] = np.uint32(rng_hw << 16)
    seed["items"]["pos_x"][0, 0] = np.float32(20.0)
    seed["items"]["pos_y"][0, 0] = np.float32(8.0)
    seed["items"]["vel_x"][0, 0] = np.float32(0.2)
    seed["items"]["timer"][0, 0] = np.float32(30.0)
    return seed


def test_sheik_thrown_needle_does_not_clank_without_article_clank_bit_negative() -> None:
    # Adjacent no-clank negative for source order ReflectDesc -> ShieldDesc -> clank -> BODY. The
    # defender's fighter HitCapsule is clank/item-interaction eligible and co-located with the Needle, but
    # Sheik's thrown-Needle command-11 hitcaps have x40_b0 clear. ftColl_8007925C requires BOTH sides'
    # x40_b0 bits before calling ftColl_80077970, so the Needle must not enter Logic109_Clanked here.
    # Hurtcaps are cleared to isolate clank from BODY.
    # refs/melee/src/melee/it/itanimlist.c::it_802790C0
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970}
    pct0 = float(_clank_seed()["percent"][0, 1])
    out = _run_needle_item_collision(_clank_seed(), clank_hitbox=True, overlap=True)
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["state"][0]) == 0
    assert float(out["percent"][1]) == pytest.approx(pct0, abs=0.01)  # no BODY damage


def test_sheik_thrown_needle_no_clank_without_eligible_overlap_negative() -> None:
    # Adjacent no-clank negatives: the Needle does NOT clank when (a) no fighter attack HitCapsule is
    # present, (b) the HitCapsule overlaps but lacks MSL_HITBOX_FLAG_CLANK, (c) it is CLANK-flagged but
    # does not overlap, or (d) it is a CATCH-element grab box. In every case the Needle stays state-0
    # (hurtcaps cleared, so no BODY confound). The co-located eligible-hitbox negative above covers the
    # article-side x40_b0 gate.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970}
    assert int(_run_needle_item_collision(_clank_seed(), clank_hitbox=False)["items"]["state"][0]) == 0
    assert (
        int(
            _run_needle_item_collision(
                _clank_seed(), clank_hitbox=True, flags=_HITBOX_FLAG_ITEM_HIT_INTERACTION
            )["items"]["state"][0]
        )
        == 0
    )
    assert (
        int(
            _run_needle_item_collision(_clank_seed(), clank_hitbox=True, overlap=False)["items"][
                "state"
            ][0]
        )
        == 0
    )
    assert (
        int(
            _run_needle_item_collision(_clank_seed(), clank_hitbox=True, element=8)["items"]["state"][
                0
            ]
        )
        == 0
    )  # MSL_HIT_ELEMENT_CATCH


def test_sheik_thrown_needle_exact_body_reject_not_widened_by_unrelated_hitbox_negative() -> None:
    # Exact lbColl BODY rejection remains authoritative even if the defender has unrelated live
    # fighter HitCapsules. ftColl_8007925C handles clank through the earlier HitCapsule-vs-HitCapsule
    # owner; a CATCH-element hitbox is not a source signal to ignore lbColl_8000805C's BODY no-hit and
    # fall back to already-live hurtcaps. The debug hurtcap override leaves a live hurtcap overlapped by
    # the Needle while the defender's source-pose matrix remains far below it, so the old broad "any
    # live hitbox" fallback would deal BODY damage here. Lock both a co-located CATCH box and a normal
    # hitbox that is live but geometrically unrelated to the Needle contact.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970}
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
    seed = _seed_needle_over_fox_defender()
    seed["items"]["pos_y"][0, 0] = np.float32(50.0)
    pct0 = float(seed["percent"][0, 1])
    catch_out = _run_needle_item_collision(
        seed,
        clank_hitbox=True,
        element=8,  # MSL_HIT_ELEMENT_CATCH
        clear_hurtcaps=False,
        override_hurtcap_under_needle=True,
    )
    far_out = _run_needle_item_collision(
        seed,
        clank_hitbox=True,
        overlap=False,
        clear_hurtcaps=False,
        override_hurtcap_under_needle=True,
    )
    for out in (catch_out, far_out):
        assert int(out["items"]["exists"][0]) == 1
        assert int(out["items"]["state"][0]) == 0
        assert int(out["hitlag"][1]) == 0
        assert float(out["percent"][1]) == pytest.approx(pct0, abs=0.01)


def test_sheik_thrown_needle_reflectdesc_wins_over_clank() -> None:
    # Precedence: ReflectDesc resolves before clank. With an active reflector pose AND an overlapping
    # eligible clank HitCapsule, the Needle REFLECTS (owner -> reflector, velocity reversed) rather than
    # clanking, and the result is identical to having no clank HitCapsule at all.
    base = _run_needle_item_collision(
        _seed_needle_over_reflecting_fox_defender(needle_y=8.0), clank_hitbox=False
    )
    withclank = _run_needle_item_collision(
        _seed_needle_over_reflecting_fox_defender(needle_y=8.0), clank_hitbox=True
    )
    assert int(withclank["items"]["owner"][0]) == 1  # reflected, not clanked
    assert float(withclank["items"]["vel_x"][0]) == pytest.approx(-0.2, abs=1e-4)
    assert int(withclank["items"]["state"][0]) == int(base["items"]["state"][0])
    assert int(withclank["items"]["owner"][0]) == int(base["items"]["owner"][0])


def test_sheik_thrown_needle_shielddesc_overlap_wins_over_clank() -> None:
    # Precedence: when ShieldDesc OVERLAPS, it resolves before clank (the shield branch HitShields and
    # returns before the clank block). The Needle is placed on the debug shield-bubble center so the
    # shield overlaps and HitShields; a co-located eligible clank HitCapsule is then a no-op -- the
    # result (HitShield + shield damage) is identical with and without it. (A shield MISS instead falls
    # through to clank/BODY, covered by the clank positive and the shield-miss->BODY test.)
    sseed = _seed_needle_over_shielding_fox_defender()
    sseed["items"]["pos_x"][0, 0] = np.float32(0.0)  # debug shield-bubble center (uninitialised shield_x)
    sseed["items"]["pos_y"][0, 0] = np.float32(8.0)
    sseed["items"]["vel_x"][0, 0] = np.float32(0.2)
    base = _run_needle_item_collision(sseed.copy(), clank_hitbox=False)
    withclank = _run_needle_item_collision(sseed.copy(), clank_hitbox=True)
    assert float(base["shield_hp"][1]) < 59.0  # the shield actually HitShielded (took Needle damage)
    assert int(withclank["items"]["owner"][0]) == 0  # not clank-transferred
    assert int(withclank["items"]["state"][0]) == int(base["items"]["state"][0])
    assert float(withclank["shield_hp"][1]) == pytest.approx(float(base["shield_hp"][1]), abs=0.01)


def test_sheik_thrown_needle_shielddesc_miss_but_body_overlap_bodies() -> None:
    # ShieldDesc MISS fallthrough to BODY (source ftColl_8007925C catch_path -> catch_elem_path on a
    # shield miss): with an active ShieldDesc whose bubble is SHRUNK by a low shield_hp so it no longer
    # encloses the lower body, a Needle below the shield bubble (y=0) misses the shield but overlaps the
    # body hurtcap and deals the ordinary 3-dmg BODY hit (state 4 bounce), taking NO shield damage. A
    # shielding defender CAN therefore be Needle-shield-poked/BODY-hit on a ShieldDesc miss; the shield
    # bit does not blanket-suppress BODY. Contrast (y=8, inside the shrunk bubble): HitShield (shield
    # damage, no BODY damage). Before the ShieldDesc-miss fallthrough fix the y=0 row deferred (state 0).
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    def _run_shrunk_shield(ndl_y: float):
        seed = _seed_needle_over_fox_defender()
        seed["action_id"][0, 1] = np.uint16(ACT_GUARD)
        seed["state_flags"][0, 1, 2] = np.uint8(0x80)  # 221B_b0 ShieldDesc active
        seed["shield_hp"][0, 1] = np.float32(10.0)  # shrink the shield bubble (hp ratio)
        seed["items"]["pos_x"][0, 0] = np.float32(20.0)
        seed["items"]["pos_y"][0, 0] = np.float32(ndl_y)
        seed["items"]["vel_x"][0, 0] = np.float32(0.2)
        shp0 = float(seed["shield_hp"][0, 1]); pct0 = float(seed["percent"][0, 1])
        return _run(seed, [_inputs_defender_holds_shield()])[0], shp0, pct0

    # Below the shrunk shield bubble: shield MISS, but BODY overlaps -> 3-dmg BODY hit + bounce.
    out, shp0, pct0 = _run_shrunk_shield(0.0)
    assert float(out["percent"][1]) == pytest.approx(pct0 + 3.0, abs=0.01)  # BODY damage (fell through)
    assert int(out["items"]["exists"][0]) == 0 or int(out["items"]["state"][0]) == 4  # bounced/destroyed
    assert float(out["shield_hp"][1]) > shp0 - 1.0  # no Needle shield damage (shield was missed)
    # Inside the shrunk bubble: HitShield (shield damage, no BODY damage) -- shield still owns overlaps.
    out2, shp0b, pct0b = _run_shrunk_shield(8.0)
    assert float(out2["percent"][1]) == pytest.approx(pct0b, abs=0.01)  # no BODY damage
    assert float(out2["shield_hp"][1]) < shp0b - 1.0  # shield took the Needle damage


def test_sheik_thrown_needle_yields_body_to_guardreflect_shielddesc_negative() -> None:
    # ShieldDesc path under the GuardReflect action: the sim's GuardReflect action update clears the
    # manually-set fp+0x2218 REFLECTING bit, so this row exercises the ShieldDesc branch (221B_b0 + live
    # shield bubble), NOT the ReflectDesc branch (which is covered by the GUARD_ON reflect tests above).
    # The Needle misses the fresh-GuardReflect shield bubble and the shield encloses the body, so the
    # contact resolves to no hit (state-0, no BODY damage). ReflectDesc precedence over ShieldDesc and
    # the reflect-miss -> ShieldDesc fallthrough are locked separately above.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80076CBC}
    seed = _seed_needle_over_fox_defender()
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD_REFLECT)
    seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)
    seed["state_flags"][0, 1, 2] = np.uint8(0x80)  # 221B_b0 ShieldDesc active
    seed["state_flags"][0, 1, 3] = np.uint8(0x20)  # 221C powershield-active
    seed["guard_reflect_timer_x14"][0, 1] = np.uint8(8)
    seed["guard_reflect_timer_x18"][0, 1] = np.uint8(12)
    pct0 = float(seed["percent"][0, 1])
    out = _run(seed, [_inputs_defender_holds_shield()])[0]
    assert float(out["percent"][1]) == pytest.approx(pct0, abs=0.01)
    assert int(out["items"]["exists"][0]) == 1
    assert int(out["items"]["state"][0]) == 0


@pytest.mark.integration
@pytest.mark.parametrize("record", [1387, 3330, 5194, 6253])
def test_sheik_vanish_airhistart0_windup_drift_matches_source_common_drift_replay_real(
    record: int,
) -> None:
    # ftSk_SpecialAirHiStart_0_Phys applies ftCommon_Fall (vanish-start gravity/terminal) then the
    # common air drift ftCommon_8007D268 -> ftCommon_8007D174. Source decelerates by aerial_friction
    # toward the stick target (capped at air_max_horizontal_velocity) once accel would overshoot,
    # rather than hard-clamping to the target; the prior hand-rolled clamp under-drifted the windup
    # (~0.8/frame). Each row below drifts >1u horizontally from the seed and now tracks ref. This
    # locks the AirHiStart0 WINDUP common-drift only; the separate AirHi post-teleport world-projection
    # residual is unrelated and not asserted here (see worklog).
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHiStart_0_Phys
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D268,ftCommon_8007D174}
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/AttractiveAnyClam.msl"
    )
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_0
    assert abs(float(ref["pos_x"][0]) - float(seed["pos_x"][0])) > 1.0
    out = _run_sample_row(samples, record)
    assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=0.05)


@pytest.mark.integration
def test_sheik_air_needle_cancel_finish_runs_fall_iasa_tail_aac_2333() -> None:
    # ftSk_SpecialAirNCancel_Anim exits through ftCo_Fall_Enter. Source then reaches the
    # destination Fall IASA tail in the same Fighter_procUpdate, so an A edge on the finish frame
    # enters AttackAirN immediately; the prior quiet frame remains in SpecialAirNCancel.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::ftSk_SpecialAirNCancel_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_IASA_Inner}
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/AttractiveAnyClam.msl"
    )
    quiet = _run_sample_row(samples, 2332)
    assert int(samples[2332]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_N_CANCEL
    assert int(samples[2332]["input_t"]["p"]["buttons"][0]) & A == 0
    assert int(quiet["action_id"][0]) == ACT_SK_SPECIAL_AIR_N_CANCEL

    out = _run_sample_row(samples, 2333)
    ref = samples[2333]["ref_t1"]
    assert int(samples[2333]["seed_t"]["action_id"][0]) == ACT_SK_SPECIAL_AIR_N_CANCEL
    assert int(samples[2333]["input_t"]["p"]["buttons"][0]) & A
    assert int(ref["action_id"][0]) == ACT_ATTACK_AIR_N
    assert int(out["action_id"][0]) == ACT_ATTACK_AIR_N
    assert int(out["animation_index"][0]) == int(ref["animation_index"][0])


# ---------------------------------------------------------------------------
# Entry-path + data-contract coverage for the thrown-Needle stage-bounce trajectory
# (SetupBounce xDD8 horizontal drift). These free-run the real aerial-throw -> descend ->
# stage-hit pipeline instead of seeding a state-4 item directly, so the live drift/stick
# lifecycle is exercised. See reports/triage/newchar_sheik/needle_source_surface_checklist.md.
# ---------------------------------------------------------------------------


def _aerial_needle_throw_seed(seed_hw: int, *, pos_y: float = 12.0) -> np.ndarray:
    seed = _seed_base("sheik")
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["pos_y"][0, 0] = np.float32(pos_y)
    seed["pos_x"][0, 1] = np.float32(500.0)  # defender far away: no contact
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_AIR_N_END)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(1)
    seed["frame_pre_random_seed"][0] = np.uint32(0x1000 + seed_hw * 7919)
    return seed


def _first_needle(row) -> tuple[int, float, float, float] | None:
    for s in range(8):
        if int(row["items"]["exists"][s]) == 1 and int(row["items"]["type"][s]) == ITEM_SHEIK_NEEDLE_THROWN:
            return (
                int(row["items"]["state"][s]),
                float(row["items"]["vel_x"][s]),
                float(row["items"]["pos_x"][s]),
                float(row["items"]["pos_y"][s]),
            )
    return None


def test_sheik_needle_bounce_x_vel_table_matches_source_it_803f7000() -> None:
    # Data contract: the bounce horizontal-drift table is it_803F7000 transcribed by source symbol,
    # routed through MSLITAR1 (no local literal copy in src/items.c). |xDD8| is selected by Randi(8)
    # and signed by Randi(2); the table is non-negative with a 0.0 first entry.
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::{it_803F7000,itSeakNeedleThrown_SetupBounce}
    import msl_binding

    table = [float(v) for v in msl_binding.item_article_params(7)["needle_bounce_x_vel"]]
    assert table == pytest.approx([0.0, 0.2, 0.4, 0.6, 0.8, 1.0, 1.2, 1.4])


def test_sheik_aerial_thrown_needle_sticks_or_bounces_at_floor_freerun() -> None:
    # Free-run the real aerial throw: the Needle is launched 45-deg down (state 0 flying), descends,
    # and on crossing the stage floor either sticks (state 2, vel 0, lingers) or bounces (state 4).
    # This is the end-to-end pass-through-floor regression, driven through the live entry path rather
    # than by injecting a state-2/4 item.
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::{itSeakneedlethrown_UnkMotion0_Coll,
    #   itSeakNeedleThrown_CheckGroundHit}
    saw_stick = False
    for hw in range(40):
        outs = _run(_aerial_needle_throw_seed(hw), [_mk_inputs() for _ in range(36)])
        settled = False
        for row in outs:
            n = _first_needle(row)
            if n is None:
                continue
            state, _vx, _px, py = n
            # The pre-fix bug let the flying (state 0) Needle sink far below the floor (y -> -48). A
            # flying Needle may sit one motion-step above/at the floor, but must never be deep below it:
            # crossing the stage line transitions it to stuck (2) or bounced (4) on that frame.
            assert not (state == 0 and py < -3.5), f"hw={hw}: flying Needle at y={py} (passed through floor)"
            if state in (2, 4):
                settled = True
            if state == 2:
                saw_stick = True
        assert settled, f"hw={hw}: Needle never settled (stuck/bounced) at the floor"
    assert saw_stick, "no seed produced a stuck (state 2) Needle"


def test_sheik_aerial_thrown_needle_bounce_drifts_horizontally_freerun() -> None:
    # Free-run regression for the SetupBounce xDD8 horizontal drift: a bounced (state 4) Needle must
    # carry vel_x equal to a signed needle_bounce_x_vel table entry and actually move in x while bounced
    # (the pre-fix sim forced vel_x = 0 so bounced Needles fell straight down).
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::{itSeakneedlethrown_UnkMotion4_Phys,
    #   itSeakNeedleThrown_SetupBounce,it_803F7000}
    import msl_binding

    x_vels = [abs(float(v)) for v in msl_binding.item_article_params(7)["needle_bounce_x_vel"]]
    saw_bounce = False
    saw_nonzero_drift = False
    for hw in range(60):
        outs = _run(_aerial_needle_throw_seed(hw), [_mk_inputs() for _ in range(36)])
        bounce_seen_x = None
        for row in outs:
            n = _first_needle(row)
            if n is None:
                continue
            state, vx, px, _py = n
            if state == 4:
                saw_bounce = True
                # vel_x must be a signed entry of the source bounce x-vel table.
                assert any(abs(vx) == pytest.approx(v) for v in x_vels), vx
                if bounce_seen_x is None:
                    bounce_seen_x = px
                elif abs(px - bounce_seen_x) > 0.5:
                    saw_nonzero_drift = True
    assert saw_bounce, "no seed produced a bounced (state 4) Needle"
    assert saw_nonzero_drift, "bounced Needles never drifted horizontally (xDD8 not applied)"


def test_sheik_needle_charge_accumulates_through_real_loop_freerun() -> None:
    # Entry-path charge accumulation: enter Neutral-B from neutral (press B), then HOLD B and free-run.
    # doEnter floors the stored count at 1; SpecialNLoop_Anim's Sheik_ChargeNeedlesIncrementer then adds
    # +1 each loop-anim cycle, climbing 1 -> 2 -> 3 ... capped at 6. The count is NEVER seeded directly,
    # so this is the live-accumulation path the prior (count-injected) tests could not exercise -- the
    # exact bug ("charge does not store more needles") that one-step reseed validation was blind to.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{doEnter,ftSk_SpecialNLoop_Anim}
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    seed = _seed_base("sheik")  # grounded neutral Wait
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, seed_stride)))
        prev = _mk_inputs()
        held_b = _mk_inputs(buttons=B)
        # Press+hold B from neutral: B-press enters SpecialN (doEnter floors count to 1); holding B
        # charges through Start -> Loop. Run long enough to cross multiple loop cycles.
        counts = []
        for _ in range(90):
            msl_binding.step_input(handle, prev, held_b)
            prev = held_b
            counts.append(int(msl_binding.debug_get_sheik_needle_count(handle, 0, 0)))
    finally:
        msl_binding.destroy(handle)

    # Monotonic non-decreasing climb from the tap floor of 1 up to (and capped at) 6 through the loop.
    assert counts[0] == 1, counts[:5]
    assert max(counts) >= 3, f"charge never accumulated past 2 through the real loop: {counts}"
    assert max(counts) <= 6, counts
    nonzero = [c for c in counts if c > 0]
    assert all(b >= a for a, b in zip(nonzero, nonzero[1:])), counts  # never decreases while charging


# ---------------------------------------------------------------------------
# Edge sweep: thrown-Needle stage-line stick directionality across floor / soft platform /
# ceiling / wall. The source CheckGroundHit -> it_8026EA20 -> mpCheckAllRemap admits a horizontal
# floor only on downward motion and a horizontal ceiling only on upward motion (walls are
# bidirectional), so soft platforms stick from above and pass through from below, exactly like any
# floor. Battlefield collision geometry is the raw stage DAT scaled by unit_scale = 0.8: the left
# side platform is y=27.2, x in [-57.6, -20.0]; the flat lower ceiling is y=-31.3, x in [-10.3, 10.3];
# a near-vertical lower right_wall is x ~ -10.3, y in [-40, -31.3].
# refs/melee/src/melee/mp/mplib.c::{mpCheckAllRemap,mpCheckFloorRemap,mpCheckCeilingRemap}
# refs/melee/src/melee/it/items/itseakneedlethrown.c::itSeakNeedleThrown_CheckGroundHit
# ---------------------------------------------------------------------------

STAGE_BATTLEFIELD = 31


def _battlefield_injected_needle(px, py, vx, vy, nframes=14):
    seed = _seed_base("sheik")
    seed["stage_id"][0] = np.uint32(STAGE_BATTLEFIELD)
    seed["pos_x"][0, 1] = np.float32(400.0)  # defender far away
    _install_sheik_item(
        seed, item_type=ITEM_SHEIK_NEEDLE_THROWN, state=0, timer=80.0,
        pos_x=px, pos_y=py, vel_x=vx, vel_y=vy,
    )
    outs = _run(seed, [_mk_inputs() for _ in range(nframes)])
    traj = []
    for o in outs:
        for s in range(8):
            if int(o["items"]["exists"][s]) == 1 and int(o["items"]["type"][s]) == ITEM_SHEIK_NEEDLE_THROWN:
                traj.append((int(o["items"]["state"][s]), float(o["items"]["pos_x"][s]), float(o["items"]["pos_y"][s])))
                break
        else:
            traj.append((-1, 0.0, 0.0))
    return traj


def test_sheik_needle_sticks_to_soft_platform_top_only_battlefield() -> None:
    # Down onto the scaled left platform (y=27.2) -> stick (state 2) just below the surface, like any
    # floor; the same x/y moving UP passes straight through (soft platforms are one-directional).
    down = _battlefield_injected_needle(-38.0, 40.0, 0.0, -3.0)
    settled = [st for (st, _x, _y) in down if st in (2, 4)]
    assert settled, f"needle never stuck descending onto the platform: {down}"
    st_end, _x_end, y_end = down[-1]
    assert st_end == 2, f"needle should rest stuck on platform top, got {down[-6:]}"
    assert 23.0 < y_end < 28.0, f"stuck y {y_end} not at the scaled platform (~27.2)"
    # A flying Needle must never be deep below the platform while still state 0 (no pass-through bug).
    assert not any(st == 0 and y < 23.0 for (st, _x, y) in down), down

    up = _battlefield_injected_needle(-38.0, 15.0, 0.0, 3.0)
    assert all(st == 0 for (st, _x, _y) in up), f"upward needle wrongly stuck under platform: {up}"
    assert up[-1][2] > 45.0, f"upward needle should pass through and keep rising: {up}"


def test_sheik_needle_sticks_to_ceiling_upward_only_battlefield() -> None:
    # Upward into the scaled flat lower ceiling (y=-31.3) -> stick (state 2).
    up = _battlefield_injected_needle(0.0, -38.0, 0.0, 3.0)
    assert up[-1][0] == 2, f"needle should stick to ceiling moving up: {up}"
    assert -33.0 < up[-1][2] < -28.0, f"ceiling stick y {up[-1][2]} not at the scaled ceiling (~-31.3)"


def test_sheik_needle_sticks_to_wall_battlefield() -> None:
    # Horizontal into the scaled near-vertical lower wall (x ~ -10.3) -> stick (state 2).
    left = _battlefield_injected_needle(-6.0, -35.0, -3.0, 0.0)
    assert left[-1][0] == 2, f"needle should stick to wall moving left: {left}"
    assert -13.5 < left[-1][1] < -9.0, f"wall stick x {left[-1][1]} not at the scaled wall (~-10.3)"


def test_sheik_needle_charge_air_to_ground_transition_preserves_count_and_article() -> None:
    # Edge: ground/air transition mid-charge. Aerial Neutral-B (SpecialAirNLoop) that lands converts to
    # the grounded SpecialNLoop via ftSk_SpecialAirNLoop_Coll (doColl), preserving the stored count
    # (fv.sk.x0) and the held Needle article (fv.sk.x4); charging then continues on the ground. The
    # symmetric ground->air conversion (SpecialNLoop_Coll) is the same source path but is not reachable
    # in free-run because a charging Sheik is stationary and never walks off a ledge.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{ftSk_SpecialAirNLoop_Coll,doColl}
    import msl_binding

    sizes = msl_binding.sizes()
    stride = int(sizes["seed"])
    seed = _seed_base("sheik")
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["pos_y"][0, 0] = np.float32(28.0)
    seed["action_id"][0, 0] = np.uint16(29)  # Fall (airborne) so the B-press enters SpecialAirN
    seed["pos_x"][0, 1] = np.float32(400.0)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(0)

    handle = msl_binding.init(batch_size=1, num_players=2)
    rows = []
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, stride)))
        prev = _mk_inputs()
        for i in range(70):
            f = _mk_inputs() if i == 0 else _mk_inputs(buttons=B)
            msl_binding.step_input(handle, prev, f)
            prev = f
            c = int(msl_binding.debug_get_sheik_needle_count(handle, 0, 0))
            ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, ob)
            r = ob.view(COMPARE_DTYPE).reshape((1,))[0]
            held = any(
                int(r["items"]["exists"][s]) == 1
                and int(r["items"]["type"][s]) == ITEM_SHEIK_NEEDLE_HELD
                for s in range(8)
            )
            rows.append((int(r["action_id"][0]), int(r["on_ground"][0]), c, held))
    finally:
        msl_binding.destroy(handle)

    # Charged aerially (SpecialAirNLoop) while airborne, holding the visual article.
    air_loop = [i for i, (a, g, _c, h) in enumerate(rows) if a == ACT_SK_SPECIAL_AIR_N_LOOP and g == 0 and h]
    assert air_loop, f"never reached aerial charge loop with held article: {rows[:10]}"
    # Landed and converted to the GROUNDED SpecialNLoop.
    gnd_loop = [i for i, (a, g, _c, _h) in enumerate(rows) if a == ACT_SK_SPECIAL_N_LOOP and g == 1]
    assert gnd_loop, f"aerial charge never converted to grounded loop on landing: {rows}"
    convert = gnd_loop[0]
    assert convert > air_loop[0], "ground loop must come after the aerial loop"
    # Count + held article survive the air->ground conversion, and charging continues on the ground.
    assert rows[convert][2] >= 1, f"count lost across transition: {rows[convert]}"
    assert rows[convert][3], f"held article lost across transition: {rows[convert]}"
    assert max(c for (_a, _g, c, _h) in rows) >= 3, f"charge did not keep accumulating: {rows}"


def _sheik_needle_volley_vel_x(face: int, turn_mid_charge: bool = False) -> list[float]:
    import msl_binding

    sizes = msl_binding.sizes()
    stride = int(sizes["seed"])
    seed = _seed_base("sheik")
    seed["pos_x"][0, 1] = np.float32(400.0)
    seed["sheik_needle_count_u8"][0, 0] = np.uint8(0)
    seed["facing"][0, 0] = np.uint8(face)
    handle = msl_binding.init(batch_size=1, num_players=2)
    vels: dict[int, float] = {}
    try:
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, stride)))
        prev = _mk_inputs()
        for i in range(150):
            if i < 100:
                mx = -100 if (turn_mid_charge and 30 < i < 60) else 0
                f = _mk_inputs(buttons=B, main_x=mx)
            else:
                f = _mk_inputs()
            msl_binding.step_input(handle, prev, f)
            prev = f
            ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
            msl_binding.write_compare(handle, ob)
            r = ob.view(COMPARE_DTYPE).reshape((1,))[0]
            for s in range(8):
                if int(r["items"]["exists"][s]) == 1 and int(r["items"]["type"][s]) == ITEM_SHEIK_NEEDLE_THROWN:
                    vels[int(r["items"]["spawn_id"][s])] = round(float(r["items"]["vel_x"][s]), 2)
    finally:
        msl_binding.destroy(handle)
    return sorted(set(vels.values()))


def test_sheik_needle_volley_direction_follows_entry_facing() -> None:
    # shootNeedles spawns each Needle along fp->facing_dir; the SpecialNLoop/End IASA only handle B and
    # LR (no turn), so the whole volley follows the facing at entry and a charge-time stick input cannot
    # flip it mid-charge. Direction ownership is the entry facing.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{shootNeedles,doIasa}
    assert _sheik_needle_volley_vel_x(1) == [pytest.approx(4.0)]  # facing right -> all needles right
    assert _sheik_needle_volley_vel_x(0) == [pytest.approx(-4.0)]  # facing left -> all needles left
    # A stick-left input during the charge does NOT turn Sheik, so a right-facing volley stays right.
    assert _sheik_needle_volley_vel_x(1, turn_mid_charge=True) == [pytest.approx(4.0)]


def test_sheik_needle_hits_non_owner_not_owner_post_reflect_targeting() -> None:
    # A thrown Needle damages any fighter EXCEPT its current owner. The shared item-reflect path swaps
    # item_owner to the reflector (tested in the reflect-precedence cases), so this owner-based BODY
    # targeting is what lets a REFLECTED Needle fly back and hit the original thrower: with owner = the
    # other port the Needle damages port 0, while an own-port Needle passes through harmlessly. This
    # also covers post-reflect multi-frame coherence (the owner stays put and the Needle keeps flying).
    # refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgDealt
    # refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C} (owner skip)
    def run_owned(owner_port: int):
        seed = _seed_base("sheik")
        seed["pos_x"][0, 0] = np.float32(0.0)
        seed["pos_x"][0, 1] = np.float32(60.0)
        seed["percent"][0, 0] = np.float32(0.0)
        _install_sheik_item(
            seed, item_type=ITEM_SHEIK_NEEDLE_THROWN, state=0, timer=40.0,
            pos_x=18.0, pos_y=4.0, vel_x=-4.0, vel_y=0.0,
        )
        seed["items"]["owner"][0, 0] = np.int8(owner_port)
        seed["items"]["instance_id"][0, 0] = np.uint16(900)
        outs = _run(seed, [_mk_inputs() for _ in range(10)])
        p1 = [round(float(o["percent"][0]), 1) for o in outs]
        owners = {
            int(o["items"]["owner"][s])
            for o in outs
            for s in range(8)
            if int(o["items"]["exists"][s]) == 1 and int(o["items"]["type"][s]) == ITEM_SHEIK_NEEDLE_THROWN
        }
        return p1, owners

    p1_other, owners_other = run_owned(1)
    p1_self, _owners_self = run_owned(0)
    assert max(p1_other) == pytest.approx(3.0), p1_other  # other-owned Needle hits port 0
    assert owners_other == {1}, owners_other  # owner stays the reflector across the flight
    assert max(p1_self) == pytest.approx(0.0), p1_self  # own Needle never self-damages


# ---------------------------------------------------------------------------
# Sheik Up-B Vanish explosion (disappear smoke article BODY hitbox). The smoke spawned at the
# disappear point carries a state-0 HitCapsule (vanish_hitbox: 12 dmg, angle 90, kbg 60, bkb 80,
# elem 1) sized by vanish_hitbox_size animated through the size keyframes, active for the article's
# first vanish_hitbox_remove_frame anim frames, hitting each non-owner once and persisting after.
# refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1C60,it_802B1D40,itSeakVanish_Logic42_DmgDealt}
# ---------------------------------------------------------------------------


def _install_vanish_smoke(seed, *, pos_x, pos_y, timer, owner=0):
    seed["items"]["exists"][0, 0] = np.uint8(1)
    seed["items"]["type"][0, 0] = np.uint16(ITEM_SHEIK_VANISH)
    seed["items"]["state"][0, 0] = np.uint8(0)
    seed["items"]["owner"][0, 0] = np.int8(owner)
    seed["items"]["instance_id"][0, 0] = np.uint16(701)
    seed["items"]["spawn_id"][0, 0] = np.uint32(71)
    seed["items"]["direction"][0, 0] = np.float32(1.0)
    seed["items"]["timer"][0, 0] = np.float32(timer)
    seed["items"]["pos_x"][0, 0] = np.float32(pos_x)
    seed["items"]["pos_y"][0, 0] = np.float32(pos_y)


def test_sheik_vanish_explosion_damages_non_owner_once_active_window() -> None:
    # A smoke article overlapping a non-owner fighter during its active window deals vanish_hitbox_damage
    # (12) exactly once (hitlist), launching them; the owner is never self-hit.
    import msl_binding

    dmg = float(msl_binding.item_article_params(7)["vanish_hitbox_damage"])
    seed = _seed_base("sheik")
    seed["pos_x"][0, 0] = np.float32(400.0)  # owner (Sheik) far away
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(0.0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["percent"][0, 1] = np.float32(0.0)
    _install_vanish_smoke(seed, pos_x=2.0, pos_y=4.0, timer=80.0, owner=0)
    outs = _run(seed, [_mk_inputs() for _ in range(8)])
    p2 = [round(float(o["percent"][1]), 1) for o in outs]
    assert max(p2) == pytest.approx(dmg), p2  # 12% dealt
    assert p2[-1] == pytest.approx(dmg), p2  # hit exactly once (no re-hit accumulation)
    assert int(outs[1]["hitstun"][1]) > 0, "vanish explosion must apply hitstun"
    # owner (Sheik, port 0) is never self-hit
    assert all(round(float(o["percent"][0]), 1) == 0.0 for o in outs), "owner self-hit"


@pytest.mark.integration
def test_sheik_demo_vanish_smoke_spawn_frame_body_hit_and_no_hitlag_rehit_lock() -> None:
    # Vanish smoke state 0 publishes an item BODY HitCapsule immediately after the accessory spawn
    # path. That live item demands fighter hurtcap endpoint geometry before item collision; the
    # following frame must not reapply while the victim is still in hitlag.
    # refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1C60,it_802B1D40}
    # refs/melee/src/melee/it/it_2725.c::it_8027518C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,Fighter_ProcessHit_8006D1EC}
    samples = _sheik_validation_samples(
        "datasets/sheik_demo_triage/replays/validation/sheik/sheik_demo_game.msl"
    )

    out = _run_sample_row(samples, 1927)
    assert int(out["action_id"][1]) == int(samples[1927]["ref_t1"]["action_id"][1]) == 90
    assert int(out["hitlag"][1]) == int(samples[1927]["ref_t1"]["hitlag"][1]) == 7
    assert float(out["percent"][1]) == pytest.approx(
        float(samples[1927]["ref_t1"]["percent"][1]), abs=1.0e-5
    )

    out = _run_sample_row(samples, 1928)
    assert int(out["hitlag"][1]) == int(samples[1928]["ref_t1"]["hitlag"][1]) == 6
    assert float(out["percent"][1]) == pytest.approx(
        float(samples[1928]["ref_t1"]["percent"][1]), abs=1.0e-5
    )


@pytest.mark.integration
def test_sheik_vanish_smoke_same_item_hitstun_seed_reconstructs_victim_ring() -> None:
    # TornEnchantingGiraffe:9258 snapshots the persistent smoke one frame after the victim leaves
    # hitlag, while hitstun still carries the same item source (`last_hit_by` + `instance_hit_by`).
    # The public seed lane for item victims is empty, so reseed must reconstruct the hidden
    # HitCapsule victim ring from source provenance instead of letting the smoke BODY re-hit.
    # refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1D40,itSeakVanish_Logic42_DmgDealt}
    # refs/melee/src/melee/it/it_2725.c::it_8027518C
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/TornEnchantingGiraffe.msl"
    )
    player = 1
    for record in (9258, 9259, 9260):
        seed = samples[record]["seed_t"]
        ref = samples[record]["ref_t1"]
        assert int(seed["items"]["type"][0]) == ITEM_SHEIK_VANISH
        assert int(seed["items"]["owner"][0]) == 0
        assert int(seed["items"]["instance_id"][0]) == int(seed["instance_hit_by"][player])
        assert int(seed["item_hitlist_victim_port"][0]) == 0xFF
        assert int(seed["hitlag"][player]) == 0
        assert int(seed["hitstun"][player]) > 0

        out = _run_sample_row(samples, record, replay_frame_rng=True)
        assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 90
        assert int(out["hitlag"][player]) == int(ref["hitlag"][player]) == 0
        assert float(out["percent"][player]) == pytest.approx(
            float(ref["percent"][player]), abs=1e-5
        )

    rollout = _run_sample_rollout_records(
        samples, start_record=9251, records=(9258, 9259, 9260), replay_frame_rng=True
    )
    for record, out in rollout.items():
        ref = samples[record]["ref_t1"]
        assert int(out["hitlag"][player]) == int(ref["hitlag"][player]) == 0
        assert float(out["percent"][player]) == pytest.approx(
            float(ref["percent"][player]), abs=1e-5
        )


@pytest.mark.integration
def test_sheik_vanish_smoke_victim_ring_seed_requires_same_item_source_negative() -> None:
    # Adjacent negative for the reseed bridge above: without the exact item instance provenance,
    # the seed does not prove an existing victims_1 entry, so the live active smoke BODY remains
    # eligible to hit the overlapping defender.
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/TornEnchantingGiraffe.msl"
    )
    player = 1
    seed = samples[9258]["seed_t"]
    assert int(seed["items"]["type"][0]) == ITEM_SHEIK_VANISH
    assert int(seed["items"]["instance_id"][0]) == int(seed["instance_hit_by"][player])

    out = _run_sample_row(
        samples,
        9258,
        replay_frame_rng=True,
        mutate_seed={"instance_hit_by": (player, 0)},
    )
    assert int(out["hitlag"][player]) > 0
    assert float(out["percent"][player]) > float(seed["percent"][player])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "player", "action_id", "expected_hitlag"),
    [
        (
            "datasets/sheik/replays/validation/sheik/ZestyPreciousTurtle.msl",
            2273,
            1,
            ACT_ATTACK_AIR_LW,
            6,
        ),
        (
            "datasets/sheik/replays/validation/sheik/MixedAllQuetzal.msl",
            9336,
            1,
            ACT_ATTACK_AIR_B,
            5,
        ),
    ],
)
def test_sheik_vanish_fresh_smoke_hitcapsule_gives_attacker_hitlag_without_body(
    dataset_rel: str, record: int, player: int, action_id: int, expected_hitlag: int
) -> None:
    # The Vanish accessory callback spawns It_Kind_Seak_Vanish and immediately publishes its item
    # HitCapsule. A same-frame opposing fighter HitCapsule can contact that fresh item HitCapsule,
    # giving attacker deal-hitlag without running the smoke BODY damage path.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{fn_80112ED8,ftSk_SpecialHi_80112F48}
    # refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1C60,it_802B1D40}
    # refs/melee/src/melee/it/it_2725.c::it_8027518C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970}
    samples = _sheik_validation_samples(dataset_rel)
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]

    assert not _item_type_present(seed, ITEM_SHEIK_VANISH)
    assert _item_type_present(ref, ITEM_SHEIK_VANISH)
    assert int(ref["items"]["damage"][0]) == 0
    assert int(seed["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_0
    assert int(ref["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(seed["action_id"][player]) == action_id
    assert int(ref["action_id"][player]) == action_id
    assert int(ref["hitlag"][player]) == expected_hitlag
    assert float(ref["percent"][player]) == pytest.approx(float(seed["percent"][player]), abs=1e-5)

    out = _run_sample_row(samples, record, replay_frame_rng=True)
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(out["action_id"][player]) == action_id
    assert int(out["hitlag"][player]) == expected_hitlag
    assert float(out["percent"][player]) == pytest.approx(float(ref["percent"][player]), abs=1e-5)
    assert _item_type_present(out, ITEM_SHEIK_VANISH)
    assert int(out["items"]["damage"][0]) == 0


@pytest.mark.integration
def test_sheik_vanish_fresh_smoke_owner_contact_hitlist_blocks_later_rehit() -> None:
    # MixedAllQuetzal:9336 is the fresh-smoke positive above. The same BAir HitCapsule also reaches
    # the invincible Vanish owner, so source ftColl has a no-damage fighter victim ring installed
    # before the item BODY pass. When the BAir's first deal-hitlag expires, MQ:9342 must not re-hit
    # that invincible owner from the same active hit group.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007925C,ftColl_80076ED8}
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/MixedAllQuetzal.msl"
    )
    player = 1

    direct = _run_sample_row(samples, 9342, replay_frame_rng=True)
    assert int(samples[9342]["ref_t1"]["hitlag"][player]) == 0
    assert int(direct["hitlag"][player]) == 0

    rollout = _run_sample_rollout_records(
        samples, start_record=9336, records=(9336, 9342), replay_frame_rng=True
    )
    assert int(rollout[9336]["hitlag"][player]) == int(samples[9336]["ref_t1"]["hitlag"][player]) == 5
    assert int(rollout[9342]["hitlag"][player]) == 0
    assert float(rollout[9342]["percent"][player]) == pytest.approx(
        float(samples[9342]["ref_t1"]["percent"][player]), abs=1e-5
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "player", "expected_hitlag"),
    [
        ("datasets/sheik/replays/validation/sheik/MixedAllQuetzal.msl", 5914, 1, 5),
        ("datasets/sheik/replays/validation/sheik/UselessGlassLoris.msl", 2167, 1, 7),
    ],
)
def test_sheik_vanish_smoke_owner_order_defers_body_after_invincible_owner_contact(
    dataset_rel: str, record: int, player: int, expected_hitlag: int
) -> None:
    # Source fighter iteration checks fighter-vs-fighter HitCapsules for the Vanish smoke owner
    # before the defender's later item BODY pass. If the defender's active aerial reaches the
    # invincible/no-damage owner first, the defender receives attacker-side deal hitlag and the smoke
    # BODY does not also launch them in that source frame.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007925C,ftColl_80076ED8}
    samples = _sheik_validation_samples(dataset_rel)
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]
    assert int(seed["action_id"][player]) == ACT_ATTACK_AIR_B
    assert int(ref["action_id"][player]) == ACT_ATTACK_AIR_B
    assert int(ref["hitlag"][player]) == expected_hitlag

    out = _run_sample_row(samples, record, replay_frame_rng=True)
    assert int(out["action_id"][player]) == ACT_ATTACK_AIR_B
    assert int(out["hitlag"][player]) == expected_hitlag
    assert float(out["percent"][player]) == pytest.approx(float(ref["percent"][player]), abs=1e-5)


def test_sheik_vanish_smoke_owner_order_defers_body_with_reversed_ports() -> None:
    # Reversed-port owner-order lock: source fighter-vs-fighter contact with the invincible Vanish
    # owner is an object/owner pass ordering lane, not a player-index ordering rule. With Sheik on
    # port 1 and the defender on port 0, the defender's clankable HitCapsule gets attacker hitlag
    # from the invincible owner contact, and the same fresh smoke item must not also BODY-damage the
    # defender in the item pass.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007925C,ftColl_80076ED8}
    # refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1C60,it_802B1D40}
    import msl_binding

    seed = _seed_base("fox", grounded=False)
    seed["char_id"][0, 1] = np.uint8(CHAR_SHEIK)
    seed["pos_x"][0, :2] = np.float32(0.0)
    seed["pos_y"][0, :2] = np.float32(0.0)
    seed["on_ground"][0, :2] = np.uint8(0)
    seed["action_id"][0, 0] = np.uint16(ACT_ATTACK_AIR_B)
    seed["animation_index"][0, 0] = np.uint32(68)
    seed["action_frame"][0, 0] = np.int16(5)
    seed["anim_frame_f32"][0, 0] = np.float32(5.0)
    seed["action_id"][0, 1] = np.uint16(ACT_SK_SPECIAL_AIR_HI_START_1)
    seed["animation_index"][0, 1] = np.uint32(312)
    seed["action_frame"][0, 1] = np.int16(0)
    seed["anim_frame_f32"][0, 1] = np.float32(35.0)
    seed["hurtbox_state"][0, 1] = np.uint8(1)
    _install_vanish_smoke(seed, pos_x=0.0, pos_y=0.0, timer=80.0, owner=1)
    seed["item_hidden_callback_flags"][0, 0] = np.uint8(1 << 1)

    sizes = msl_binding.sizes()
    seed_bytes = seed.view(np.uint8).reshape((1, int(sizes["seed"]))).copy()
    input_bytes = _mk_inputs().view(np.uint8).reshape((1, int(sizes["input"]))).copy()
    out_bytes = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        msl_binding.reseed_seed(handle, seed_bytes)
        msl_binding.debug_refresh_combat_geometry(handle)
        msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
        msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 50.0, 9.0, 1)
        msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, HIT_GROUNDED | HIT_AERIAL)
        msl_binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
        msl_binding.step_input(handle, input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_bytes)
    finally:
        msl_binding.destroy(handle)

    out = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    assert int(out["action_id"][0]) == ACT_ATTACK_AIR_B
    assert int(out["hitlag"][0]) == 7
    assert float(out["percent"][0]) == pytest.approx(0.0)


@pytest.mark.integration
def test_sheik_vanish_smoke_stale_owner_action_does_not_defer_to_attacker_hitlag() -> None:
    # UnusedLivelyLouse:5483 has a lingering Vanish smoke article, but the owner is no longer in the
    # Vanish travel-entry action that owns the source fighter-contact ordering lane. Marth's Dolphin
    # Slash HitCapsules therefore do not receive attacker deal-hitlag from the old smoke owner.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialAirHiStart_1_Anim
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/UnusedLivelyLouse.msl"
    )
    record = 5483
    player = 1
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]

    assert int(seed["items"]["type"][0]) == ITEM_SHEIK_VANISH
    assert int(seed["items"]["owner"][0]) == 0
    assert int(seed["action_id"][0]) not in (
        ACT_SK_SPECIAL_HI_START_1,
        ACT_SK_SPECIAL_AIR_HI_START_1,
    )

    out = _run_sample_row(samples, record, replay_frame_rng=True)
    assert int(out["action_id"][player]) == int(ref["action_id"][player]) == 368
    assert int(out["hitlag"][player]) == int(ref["hitlag"][player]) == 0
    assert float(out["percent"][player]) == pytest.approx(float(ref["percent"][player]), abs=1e-5)


@pytest.mark.integration
@pytest.mark.parametrize("record", [8538, 8540])
def test_sheik_vanish_smoke_owner_order_ignores_nonclank_specialhi_hold_pulses(
    record: int,
) -> None:
    # Fox SpecialHiHoldAir creates alternating, non-clank fire HitCapsules during the same Vanish
    # smoke owner frame. They are real damaging hitboxes, but they are not the clankable
    # ftColl_80078C70 ordering lane that preempts Sheik smoke BODY in the BAir positives above.
    # Keep the discriminator on the extracted create_hitbox `clank` bit, not the replay row/action.
    # data/moves/fox.json::specials_by_msid[308].events.create_hitbox.clank=false
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007925C}
    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/MixedAllQuetzal.msl"
    )
    player = 1
    seed = samples[record]["seed_t"]
    ref = samples[record]["ref_t1"]

    assert int(seed["items"]["type"][0]) == ITEM_SHEIK_VANISH
    assert int(seed["items"]["owner"][0]) == 0
    assert int(seed["action_id"][0]) == ACT_SK_SPECIAL_AIR_HI_START_1
    assert int(seed["action_id"][player]) == ACT_FX_SPECIAL_HI_HOLD_AIR
    assert int(ref["hitlag"][player]) == 0

    out = _run_sample_row(samples, record, replay_frame_rng=True)
    assert int(out["action_id"][player]) == ACT_FX_SPECIAL_HI_HOLD_AIR
    assert int(out["hitlag"][player]) == 0
    assert float(out["percent"][player]) == pytest.approx(float(ref["percent"][player]), abs=1e-5)


def test_sheik_vanish_explosion_inactive_after_remove_frame() -> None:
    # Past vanish_hitbox_remove_frame the HitCapsule is gone: a smoke whose article age exceeds the
    # remove frame deals no damage even while overlapping the opponent (still visually present).
    import msl_binding

    ap = msl_binding.item_article_params(7)
    lifetime = int(ap["sheik_vanish_lifetime_frames"])
    remove = int(ap["vanish_hitbox_remove_frame"])
    seed = _seed_base("sheik")
    seed["pos_x"][0, 0] = np.float32(400.0)
    seed["pos_x"][0, 1] = np.float32(0.0)
    seed["pos_y"][0, 1] = np.float32(0.0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["percent"][0, 1] = np.float32(0.0)
    # timer corresponding to age = remove + 4 (well past the active window)
    _install_vanish_smoke(seed, pos_x=2.0, pos_y=4.0, timer=float(lifetime - remove - 4), owner=0)
    outs = _run(seed, [_mk_inputs() for _ in range(6)])
    assert all(round(float(o["percent"][1]), 1) == 0.0 for o in outs), "hit past remove frame"


def test_sheik_vanish_explosion_freerun_grounded_upb_damages_adjacent_opponent() -> None:
    # Entry-path: a grounded Up-B (B + up) next to a grounded opponent disappears and the explosion
    # deals vanish_hitbox_damage, driven entirely by real inputs (no injected item or seeded percent).
    import msl_binding

    dmg = float(msl_binding.item_article_params(7)["vanish_hitbox_damage"])
    seed = _seed_base("sheik")  # default grounded
    seed["pos_x"][0, 0] = np.float32(0.0)
    seed["pos_x"][0, 1] = np.float32(7.0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["percent"][0, 1] = np.float32(0.0)
    frames = [_mk_inputs(buttons=B, main_y=100)] + [_mk_inputs(main_y=100) for _ in range(45)]
    outs = _run(seed, frames)
    # Sheik actually entered Vanish (Hi states) and the opponent took the explosion damage.
    assert any(355 <= int(o["action_id"][0]) <= 360 for o in outs), "never entered Vanish"
    assert max(round(float(o["percent"][1]), 1) for o in outs) == pytest.approx(dmg), [
        round(float(o["percent"][1]), 1) for o in outs
    ]


def test_sheik_vanish_explosion_never_hits_owner_when_overlapping() -> None:
    # Owner-skip proof: the smoke's OWNER (Sheik, port 0) is placed directly on the smoke so the
    # explosion sphere encloses the owner's hurtcaps, yet the owner takes zero damage (def == owner is
    # skipped). The non-owner is moved far away so the assertion is unambiguous.
    seed = _seed_base("sheik")
    seed["pos_x"][0, 0] = np.float32(0.0)  # owner overlaps the smoke
    seed["pos_y"][0, 0] = np.float32(0.0)
    seed["on_ground"][0, 0] = np.uint8(1)
    seed["percent"][0, 0] = np.float32(0.0)
    seed["pos_x"][0, 1] = np.float32(400.0)  # non-owner far away
    _install_vanish_smoke(seed, pos_x=2.0, pos_y=4.0, timer=80.0, owner=0)
    outs = _run(seed, [_mk_inputs() for _ in range(6)])
    assert all(round(float(o["percent"][0]), 1) == 0.0 for o in outs), [
        round(float(o["percent"][0]), 1) for o in outs
    ]


def _seed_vanish_smoke_over_shielding_defender(*, smoke_y=8.0, timer=80.0, shield_hp=60.0):
    # Owner (Sheik, port 0) far away; the smoke is owned by port 0 and placed on the shielding
    # non-owner (Fox, port 1) holding an active ShieldDesc (Guard + 221B_b0).
    seed = _seed_base("sheik")
    seed["pos_x"][0, 0] = np.float32(400.0)
    seed["pos_x"][0, 1] = np.float32(20.0)
    seed["pos_y"][0, 1] = np.float32(0.0)
    seed["on_ground"][0, 1] = np.uint8(1)
    seed["action_id"][0, 1] = np.uint16(ACT_GUARD)
    seed["animation_index"][0, 1] = np.uint32(0xFFFFFFFF)
    seed["state_flags"][0, 1, 2] = np.uint8(0x80)  # 221B_b0 ShieldDesc active
    seed["shield_hp"][0, 1] = np.float32(shield_hp)
    seed["percent"][0, 1] = np.float32(0.0)
    _install_vanish_smoke(seed, pos_x=20.0, pos_y=smoke_y, timer=timer, owner=0)
    return seed


def _vanish_smoke_on_shielded_body_seed(*, shield_hp):
    # An age-4 explosion sphere placed on the shielding defender's refreshed Guard body hurtcap.
    # Shield bubble center is ~(22.6, 8.6): at full shield_hp the bubble reaches the body and overlaps
    # the explosion; shrunk it does not. Pose is held identical (Guard + animation_index 0xFFFFFFFF)
    # across both so ONLY the shield size differs.
    import msl_binding

    ap = msl_binding.item_article_params(7)
    # Age 4 sits on the create-size -> first-keyframe ramp (~size 6.6): big enough to reach the body
    # hurtcap from outside a shrunk shield, small enough that a full shield bubble cleanly encloses it.
    age = 4
    seed = _seed_vanish_smoke_over_shielding_defender(
        smoke_y=0.0, timer=float(int(ap["sheik_vanish_lifetime_frames"]) - age), shield_hp=shield_hp
    )
    seed["items"]["pos_x"][0, 0] = np.float32(12.0)
    return seed


def test_sheik_vanish_explosion_active_shield_overlap_absorbs_body() -> None:
    # ShieldDesc contact: a FULL shield bubble overlapping the explosion ABSORBS it -- no fighter percent
    # damage. Non-vacuous: the SHRUNK-shield control (same pose/position) misses the bubble and the body
    # takes the full 12, proving the body is reachable and the full shield is what blocked it.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
    import msl_binding

    dmg = float(msl_binding.item_article_params(7)["vanish_hitbox_damage"])
    absorbed = _run(
        _vanish_smoke_on_shielded_body_seed(shield_hp=60.0), [_inputs_defender_holds_shield()]
    )[0]
    assert float(absorbed["percent"][1]) == pytest.approx(0.0)  # full shield absorbs the BODY
    control = _run(
        _vanish_smoke_on_shielded_body_seed(shield_hp=8.0), [_inputs_defender_holds_shield()]
    )[0]
    assert float(control["percent"][1]) == pytest.approx(dmg)  # body IS reachable when shield misses


def test_sheik_vanish_explosion_shield_miss_falls_through_to_body() -> None:
    # ShieldDesc MISS -> BODY fallthrough (source item-vs-fighter contact order): with the shield bubble
    # SHRUNK by a low shield_hp so it no longer reaches the body, the same explosion sphere misses the
    # shield but overlaps the body hurtcap and deals BODY damage (the shield-hit `continue` is inside the
    # overlap branch only, so a shield miss does not suppress BODY).
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    import msl_binding

    dmg = float(msl_binding.item_article_params(7)["vanish_hitbox_damage"])
    out = _run(
        _vanish_smoke_on_shielded_body_seed(shield_hp=8.0), [_inputs_defender_holds_shield()]
    )[0]
    assert float(out["percent"][1]) == pytest.approx(dmg)  # shrunk shield missed -> BODY hit


# ---------------------------------------------------------------------------
# Sheik Side-B Chain — free-run lifecycle + owner-tracking + live whip geometry + hitbox damage.
# Implemented: state machine / article lifecycle / states / death cleanup; the it_802BC080 stick-driven
# whip link solve (src/items.c::sheik_chain_solve_links); the 4 fighter HitCapsules repositioned along
# the solved links via the it_802BCB88 stride map and published through the normal combat path
# (src/hitboxes.c override). "Logic54" is the chain's retract-completion, NOT an opponent grab (Sheik's
# Chain has no grab mechanic). The only remaining caveat is that the chain link geometry is hidden
# accumulating state, so the chain hitbox POSITIONS are free-run behavior, not one-step-reconstructible
# (damage/hit outcomes match). See chain_source_surface_checklist.md.
# Grounded Side-B needs a diagonal (B + main_x + main_y); pure horizontal routes to Dash. Aerial Side-B
# is B + main_x (a diagonal-up would route to Up-B).
# refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c
# refs/melee/src/melee/it/items/itseakchain.c::{it_802BC080,it_802BCB88}
# ---------------------------------------------------------------------------


def _chain_slots(row):
    return [
        s
        for s in range(8)
        if int(row["items"]["exists"][s]) == 1 and int(row["items"]["type"][s]) == ITEM_SHEIK_CHAIN
    ]


def test_sheik_chain_grounded_lifecycle_freerun() -> None:
    # Drive grounded Side-B from a neutral stand: Chain article spawns mid-Start, persists through the
    # held S state, then retracts and is destroyed during S_END; Sheik returns to a normal action.
    seed = _seed_base("sheik")
    seed["pos_x"][0, 1] = np.float32(400.0)
    frames = (
        [_mk_inputs(buttons=B, main_x=80, main_y=80)]
        + [_mk_inputs(buttons=B, main_x=80, main_y=20) for _ in range(45)]
        + [_mk_inputs(main_x=0) for _ in range(45)]
    )
    outs = _run(seed, frames)
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_SK_SPECIAL_S_START in acts, acts[:3]  # entered grounded Side-B
    assert ACT_SK_SPECIAL_S in acts  # reached the held swing state
    assert ACT_SK_SPECIAL_S_END in acts  # released into End
    # Chain article appears, then is destroyed by the End; article states are the source set {0,1,3,4}.
    states = {
        int(o["items"]["state"][s]) for o in outs for s in _chain_slots(o)
    }
    assert states.issubset({0, 1, 3, 4}) and states, states
    spawned = any(_chain_slots(o) for o in outs)
    gone_after = not _chain_slots(outs[-1])
    assert spawned and gone_after, "chain must spawn then be destroyed by S_END"


def test_sheik_chain_article_tracks_owner_freerun() -> None:
    # Retained fix: the Chain item root remains the source spawn position, but fn_802BB44C/fn_802BB694
    # sample the live owner's L3rdNa link target every frame. With Sheik drifting in the air, the
    # Chain target-to-owner x offset stays in the hand-swing band instead of behaving like a detached
    # world-space article. refs/melee/src/melee/it/items/itseakchain.c::{fn_802BB44C,fn_802BB694}
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base("sheik")
        seed["on_ground"][0, 0] = np.uint8(0)
        seed["pos_y"][0, 0] = np.float32(80.0)
        seed["action_id"][0, 0] = np.uint16(29)
        seed["speed_air_x_self"][0, 0] = np.float32(3.0)  # drift right
        seed["pos_x"][0, 1] = np.float32(400.0)
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        out = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        prev = _mk_inputs()
        frames = [_mk_inputs(buttons=B, main_x=80)] + [
            _mk_inputs(buttons=B, main_x=80) for _ in range(55)
        ]
        rels = []
        owner_xs = []
        for inp in frames:
            msl_binding.step_input(handle, prev, inp)
            prev = inp
            d = msl_binding.sheik_chain_debug(handle, 0, 0)
            if d is None or int(d["item"]["target_valid"]) == 0:
                continue
            msl_binding.write_compare(handle, out)
            row = out.view(COMPARE_DTYPE).reshape((1,))[0]
            rels.append(float(d["item"]["target_x"]) - float(row["pos_x"][0]))
            owner_xs.append(float(row["pos_x"][0]))
    finally:
        msl_binding.destroy(handle)

    assert len(rels) >= 8, "chain target never present during the aerial drift"
    owner_move = max(owner_xs) - min(owner_xs)
    assert owner_move > 20.0, f"owner did not move enough to exercise the bug: {owner_move}"
    rel_band = max(rels) - min(rels)
    assert rel_band < 8.0, rels
    assert rel_band < 0.25 * owner_move, (rel_band, owner_move)


def test_sheik_chain_aerial_entry_freerun() -> None:
    # Aerial Side-B (B + forward) enters the aerial Chain family and spawns the article.
    seed = _seed_base("sheik")
    seed["on_ground"][0, 0] = np.uint8(0)
    seed["pos_y"][0, 0] = np.float32(40.0)
    seed["action_id"][0, 0] = np.uint16(29)
    seed["pos_x"][0, 1] = np.float32(400.0)
    outs = _run(seed, [_mk_inputs(buttons=B, main_x=80)] + [_mk_inputs(buttons=B, main_x=80) for _ in range(45)])
    acts = [int(o["action_id"][0]) for o in outs]
    assert ACT_SK_SPECIAL_AIR_S_START in acts, acts[:3]
    assert ACT_SK_SPECIAL_AIR_S in acts  # reached the aerial held swing state
    assert any(_chain_slots(o) for o in outs), "aerial chain article never spawned"


def _chain_whip_frames():
    # Grounded Side-B, then oscillate the stick to drive the it_802BC080 whip so the chain hitboxes
    # (repositioned along the solved links by the it_802BCB88 stride map) sweep through the swing arc.
    frames = [_mk_inputs(buttons=B, main_x=80, main_y=80)]
    for k in range(50):
        sy = 80 if (k // 3) % 2 == 0 else -80
        frames.append(_mk_inputs(buttons=B, main_x=80, main_y=sy))
    return frames


def test_sheik_chain_whip_hits_near_opponent_freerun() -> None:
    # Positive free-run lock: the live Chain whip (solved Verlet links published as the 4 fighter
    # HitCapsules) damages an opponent standing inside the swing arc just in front of/below Sheik.
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_UpdateHitboxes
    seed = _seed_base("sheik")
    seed["pos_x"][0, 1] = np.float32(8.0)
    seed["pos_y"][0, 1] = np.float32(0.0)
    outs = _run(seed, _chain_whip_frames())
    acts = {int(o["action_id"][0]) for o in outs}
    assert ACT_SK_SPECIAL_S in acts, "never reached the held chain swing"
    assert any(_chain_slots(o) for o in outs), "chain article never present"
    max_pct = max(float(o["percent"][1]) for o in outs)
    assert max_pct > 0.0, "chain whip never damaged the in-arc opponent"


def test_sheik_chain_whip_misses_distant_opponent_negative() -> None:
    # Adjacent-negative free-run lock: an opponent beyond the chain's reach (~link_count*segment from
    # the hand) is NOT hit by the same swing, so the published hitboxes are the bounded solved-link
    # geometry rather than a stage-wide stack. Pairs with the positive lock above.
    seed = _seed_base("sheik")
    seed["pos_x"][0, 1] = np.float32(60.0)
    seed["pos_y"][0, 1] = np.float32(0.0)
    outs = _run(seed, _chain_whip_frames())
    acts = {int(o["action_id"][0]) for o in outs}
    assert ACT_SK_SPECIAL_S in acts, "never reached the held chain swing"
    assert any(_chain_slots(o) for o in outs), "chain article never present"
    max_pct = max(float(o["percent"][1]) for o in outs)
    assert max_pct == 0.0, f"chain whip falsely hit a far ({60.0}) opponent for {max_pct}%"


@pytest.mark.integration
def test_sheik_demo_chain_active_frontier_hits_and_hitlag_freezes_replay_real() -> None:
    # Replay-real active-frontier lock for the official Sheik demo Chain contact sequence:
    # - rec304/rec330/rec340/rec354 are adjacent quiet frames (no early or cooldown-fallout Chain hit),
    # - rec312/rec322/rec341 are source Chain BODY hits from the it_802BCB88-published frontier,
    # - the published Chain hitcaps freeze while the owning Sheik is in hitlag, and source-probed
    #   full x1C activation windows keep the hitcap cooldown frozen with them.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    #   ftSk_SpecialS_80110BCC,ftSk_SpecialS_UpdateHitboxes}
    # refs/melee/src/melee/it/items/itseakchain.c::{it_802BC080,it_802BCB88}
    import msl_binding

    samples = _sheik_validation_samples("datasets/sheik/replays/validation/sheik/sheik_demo_game.msl")
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)
    wanted = {304, 312, 313, 315, 316, 317, 322, 323, 330, 340, 341, 342, 353, 354, 355, 356}
    outs: dict[int, np.void] = {}
    dbg: dict[int, dict] = {}

    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed_bytes = samples[0:1]["seed_t"].view(np.uint8).reshape((1, seed_stride)).copy()
        msl_binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(0, max(wanted) + 1):
            row = samples[record : record + 1]
            msl_binding.step_input(
                handle,
                row["prev_input_t"].view(np.uint8).reshape((1, input_stride)).copy(),
                row["input_t"].view(np.uint8).reshape((1, input_stride)).copy(),
            )
            if record in wanted:
                msl_binding.write_compare(handle, out_bytes)
                outs[record] = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
                dbg[record] = msl_binding.sheik_chain_debug(handle, 0, 0)
    finally:
        msl_binding.destroy(handle)

    def assert_row(record: int) -> None:
        ref = samples[record]["ref_t1"]
        out = outs[record]
        assert int(out["hitlag"][0]) == int(ref["hitlag"][0])
        assert int(out["action_id"][1]) == int(ref["action_id"][1])
        assert int(out["hitlag"][1]) == int(ref["hitlag"][1])
        assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]))

    for record in sorted(wanted):
        assert_row(record)
    assert int(outs[304]["hitlag"][0]) == 0
    assert int(outs[330]["hitlag"][0]) == 0
    assert int(outs[330]["hitlag"][1]) == 0
    assert int(outs[340]["hitlag"][0]) == 0
    assert int(outs[354]["hitlag"][0]) == 0
    assert int(outs[354]["hitlag"][1]) == 0
    assert int(outs[355]["hitlag"][0]) == 0
    assert int(outs[356]["hitlag"][0]) == 0
    assert (int(outs[312]["hitlag"][0]), int(outs[312]["hitlag"][1])) == (4, 6)
    assert (int(outs[322]["hitlag"][0]), int(outs[322]["hitlag"][1])) == (4, 6)
    assert (int(outs[341]["hitlag"][0]), int(outs[341]["hitlag"][1])) == (4, 6)
    assert float(outs[312]["percent"][1]) == pytest.approx(11.0)
    assert float(outs[322]["percent"][1]) == pytest.approx(16.0)
    assert float(outs[330]["percent"][1]) == pytest.approx(16.0)
    assert float(outs[341]["percent"][1]) == pytest.approx(21.0)
    assert float(outs[354]["percent"][1]) == pytest.approx(21.0)

    def hb3_xy(record: int) -> tuple[float, float]:
        hx, hy, active = dbg[record]["hitboxes"][3]
        assert active == 1
        return (float(hx), float(hy))

    # Owner hitlag freezes the article's published Chain geometry; publication resumes once the
    # owner's hitlag drains, and the next hits freeze again.
    assert hb3_xy(313) == pytest.approx(hb3_xy(312))
    assert hb3_xy(315) == pytest.approx(hb3_xy(312))
    assert hb3_xy(316) == pytest.approx(hb3_xy(312))
    assert abs(hb3_xy(317)[0] - hb3_xy(312)[0]) > 0.5
    assert hb3_xy(323) == pytest.approx(hb3_xy(322))
    assert hb3_xy(342) == pytest.approx(hb3_xy(341))


@pytest.mark.integration
def test_sheik_demo2_chain_publication_freezes_stale_damage_replay_real() -> None:
    # Demo2 Chain stale-damage lock:
    # - the Chain hitcaps are published from it_802BCB88/ftSk_SpecialS_UpdateHitboxes after Start
    #   script creation, and HitCapsule.damage is frozen with the stale table state at publication;
    # - later same-attack Chain hits have already inserted the held Chain instance into the stale
    #   queue, but must not retroactively restale the existing capsule at contact time.
    # The mutated adjacent negative clears the pre-existing stale Chain entry at the rollout start;
    # damage then rises, proving the lock follows the stale table rather than a replay-row constant.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0
    # refs/melee/src/melee/it/items/itseakchain.c::it_802BCB88
    # refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_UpdateHitboxes
    import msl_binding

    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/sheik_demo_game_2.msl"
    )
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

    def roll(start_record: int, wanted: tuple[int, ...], *, clear_chain_stale: bool = False):
        handle = msl_binding.init(batch_size=1, num_players=2, ucf_enabled=True, ucf_cardinals_1_0_enabled=True)
        outs: dict[int, np.void] = {}
        try:
            seed_row = samples[start_record : start_record + 1]["seed_t"].copy()
            if clear_chain_stale:
                seed_row["stale_queue_index"][0, 0] = np.uint8(0)
                seed_row["stale_move_id"][0, 0, :] = np.uint16(0)
                seed_row["stale_attack_instance"][0, 0, :] = np.uint16(0)
            msl_binding.reseed_seed_rollout(
                handle, seed_row.view(np.uint8).reshape((1, seed_stride)).copy()
            )
            for record in range(start_record, max(wanted) + 1):
                row = samples[record : record + 1]
                msl_binding.step_input_replay_frame_rng(
                    handle,
                    row["seed_t"].view(np.uint8).reshape((1, seed_stride)).copy(),
                    row["prev_input_t"].view(np.uint8).reshape((1, input_stride)).copy(),
                    row["input_t"].view(np.uint8).reshape((1, input_stride)).copy(),
                )
                if record in wanted:
                    msl_binding.write_compare(handle, out_bytes)
                    outs[record] = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
            return outs
        finally:
            msl_binding.destroy(handle)

    rows = roll(2118, (2572, 2575, 2578, 2598))
    for record, out in rows.items():
        ref = samples[record]["ref_t1"]
        assert int(out["action_id"][1]) == int(ref["action_id"][1])
        assert int(out["hitlag"][1]) == int(ref["hitlag"][1])
        assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]), abs=1e-6)
        assert float(out["speed_y_attack"][1]) == pytest.approx(
            float(ref["speed_y_attack"][1]), abs=1e-6
        )

    unstaled = roll(2118, (2572,), clear_chain_stale=True)[2572]
    assert float(unstaled["percent"][1]) > float(samples[2572]["ref_t1"]["percent"][1]) + 0.3


@pytest.mark.integration
def test_sheik_demo2_chain_reseed_clears_runtime_hitcap_frontier_negative() -> None:
    # Replay-real negative for rollout locator retries: Sheik Chain ItemLink/x2C_b0 frontier state
    # and the it_802BCB88-published fighter HitCapsule map are runtime-only article lanes. A reused
    # rollout handle that just stepped an earlier seeded row must not carry those private hitcaps
    # into a later seed whose public item snapshot does not contain a live Chain article.
    # refs/melee/src/melee/it/items/itseakchain.c::{
    #   it_802BAF2C,it_802BBD64,it_802BC080,it_802BCB88}
    import msl_binding

    samples = _sheik_validation_samples(
        "datasets/sheik/replays/validation/sheik/sheik_demo_game_2.msl"
    )
    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    out_bytes = np.zeros((1, compare_stride), dtype=np.uint8)

    def step_normal(handle, record: int) -> np.void:
        row = samples[record : record + 1]
        msl_binding.step_input(
            handle,
            row["prev_input_t"].view(np.uint8).reshape((1, input_stride)).copy(),
            row["input_t"].view(np.uint8).reshape((1, input_stride)).copy(),
        )
        msl_binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()

    def step_replay_rng(handle, record: int) -> np.void:
        row = samples[record : record + 1]
        msl_binding.step_input_replay_frame_rng(
            handle,
            row["seed_t"].view(np.uint8).reshape((1, seed_stride)).copy(),
            row["prev_input_t"].view(np.uint8).reshape((1, input_stride)).copy(),
            row["input_t"].view(np.uint8).reshape((1, input_stride)).copy(),
        )
        msl_binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()

    handle = msl_binding.init(
        batch_size=1, num_players=2, ucf_enabled=True, ucf_cardinals_1_0_enabled=True
    )
    try:
        msl_binding.reseed_seed_rollout(
            handle,
            samples[0:1]["seed_t"].view(np.uint8).reshape((1, seed_stride)).copy(),
        )
        cur = 0
        for wait_record in (479, 1379):
            for record in range(cur, wait_record + 1):
                _ = step_normal(handle, record)
            # Exercise the locator's same-handle seeded retry at proven Wait-RNG roots; the second
            # retry used to leave Chain hitcaps active for the later rec1626 seed.
            msl_binding.reseed_seed_rollout(
                handle,
                samples[wait_record : wait_record + 1]["seed_t"]
                .view(np.uint8)
                .reshape((1, seed_stride))
                .copy(),
            )
            _ = step_replay_rng(handle, wait_record)
            cur = wait_record + 1
        for record in range(cur, 1626):
            _ = step_normal(handle, record)
        msl_binding.reseed_seed_rollout(
            handle,
            samples[1626:1627]["seed_t"].view(np.uint8).reshape((1, seed_stride)).copy(),
        )
        out = step_replay_rng(handle, 1626)
    finally:
        msl_binding.destroy(handle)

    ref = samples[1626]["ref_t1"]
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == 81
    assert int(out["hitlag"][1]) == int(ref["hitlag"][1]) == 0
    assert int(out["hitstun"][1]) == int(ref["hitstun"][1]) == 6
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]))
    assert float(out["percent"][1]) == pytest.approx(8.0)


def _chain_geometry_series(flick: bool):
    # Drive grounded Side-B and sample the solved Chain geometry each frame via the test-only getter
    # msl_binding.sheik_chain_debug -> {"links": [(x,y)...], "hitboxes": [(x,y,active)...]}.
    import msl_binding

    sizes = msl_binding.sizes()
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        seed = _seed_base("sheik")
        seed["pos_x"][0, 1] = np.float32(400.0)
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        prev = _mk_inputs()
        frames = [_mk_inputs(buttons=B, main_x=80, main_y=80)]
        for k in range(45):
            sy = (80 if (k // 3) % 2 == 0 else -80) if flick else 20
            frames.append(_mk_inputs(buttons=B, main_x=80, main_y=sy))
        series = []
        for inp in frames:
            msl_binding.step_input(handle, prev, inp)
            prev = inp
            d = msl_binding.sheik_chain_debug(handle, 0, 0)
            if d is not None:
                series.append(d)
        return series
    finally:
        msl_binding.destroy(handle)


def test_sheik_chain_solved_geometry_is_stick_driven_and_respects_segment_bounds() -> None:
    # Direct geometry lock: prove the published Chain hitboxes ride the it_802BC080 solved whip, not a
    # static bone pose. (1) Adjacent links stay within one segment length (the x4 constraint). (2)
    # the stick-history impulses move the source target and the solved tail follows that higher
    # target instead of staying on the held-stick baseline. (3) Each fighter HitCapsule sits exactly
    # on the link the it_802BCB88 stride map assigns it.
    # refs/melee/src/melee/it/items/itseakchain.c::{it_802BC080,it_802BCB88}
    flick = _chain_geometry_series(flick=True)
    steady = _chain_geometry_series(flick=False)
    assert len(flick) >= 8 and len(steady) >= 8, (len(flick), len(steady))

    seg = 1.5  # sheik_chain_segment_length (attr x4)
    for d in flick:
        links = d["links"]
        active = d["active_links"]
        assert len(links) == 20  # sheik_chain_link_count
        for i, (a, b) in enumerate(zip(links, links[1:])):
            if active[i] == 0 or active[i + 1] == 0:
                continue
            dist = ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2) ** 0.5
            assert dist <= seg * 1.10, dist  # source link/collision solve stays segment-bounded

    def y_range(series, selector):
        ys = [selector(d) for d in series]
        return max(ys) - min(ys)

    flick_target_range = y_range(flick, lambda d: d["item"]["target_y"])
    steady_target_range = y_range(steady, lambda d: d["item"]["target_y"])
    assert flick_target_range > 2.0, flick_target_range
    assert flick_target_range > 4.0 * steady_target_range, (
        flick_target_range,
        steady_target_range,
    )
    assert flick[-1]["links"][-1][1] > steady[-1]["links"][-1][1] + 5.0

    # The 4 fighter HitCapsules track the solved links via the it_802BCB88 active-frontier stride
    # map. Each published hitbox equals its mapped link, proving the combat path consumes the solved
    # whip geometry rather than the static script position.
    mapped_frames = 0
    for d in flick:
        links = d["links"]
        for hb_id, link_idx in enumerate(d["hitbox_link_idx"]):
            if int(link_idx) == 0xFF:
                continue
            mapped_frames += 1
            hx, hy, _active = d["hitboxes"][hb_id]
            lx, ly = links[int(link_idx)][:2]
            assert abs(hx - lx) < 1e-3 and abs(hy - ly) < 1e-3, (hb_id, (hx, hy), (lx, ly))
    assert mapped_frames >= 8
