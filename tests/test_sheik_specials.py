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

from test_char_common_action_coverage import _mk_inputs, _run, _seed_base  # noqa: E402
from tools.eval.dataset import COMPARE_DTYPE, read_dataset  # noqa: E402

pytest.importorskip("msl_binding")

A = 0x0100
B = 0x0200

ACT_WAIT = 0x000E
ACT_FALL = 0x001D
ACT_JUMP_F = 0x0019
ACT_LANDING = 0x002A
ACT_LANDING_FALL_SPECIAL = 0x002B
ACT_LANDING_AIR_N = 0x0046
ACT_GUARD_ON = 0x00B2
ACT_FX_SPECIAL_N_START = 0x0155
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
ACT_SK_SPECIAL_AIR_LW = 363
ACT_CLIFF_CATCH = 252

SM_LANDING = 35
SM_LANDING_AIR_N = 73

ITEM_SHEIK_NEEDLE_THROWN = 79
ITEM_SHEIK_NEEDLE_HELD = 80


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


def _run_sample_row(samples: np.ndarray, record: int, *, mutate_input_y: int | None = None) -> np.void:
    import msl_binding

    row = samples[record : record + 1].copy()
    if mutate_input_y is not None:
        row["input_t"]["p"]["main_y"][0, 0] = np.int8(mutate_input_y)

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
        msl_binding.step_input(handle, prev_input_bytes, input_bytes)
        msl_binding.write_compare(handle, out_bytes)
        return out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        msl_binding.destroy(handle)


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


def test_sheik_chain_air_start_no_floor_contact_stays_air_start_negative() -> None:
    seed = _seed_base("sheik", grounded=False, pos_y=80.0)
    seed["action_id"][0, 0] = np.uint16(ACT_SK_SPECIAL_AIR_S_START)
    seed["animation_index"][0, 0] = np.uint32(304)
    seed["anim_frame_f32"][0, 0] = np.float32(5.0)

    out = _run(seed, [_mk_inputs(buttons=B)])[0]
    assert int(out["action_id"][0]) == ACT_SK_SPECIAL_AIR_S_START


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
