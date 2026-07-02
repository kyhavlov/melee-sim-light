from __future__ import annotations

import json
import re
import numpy as np
import pytest
from pathlib import Path

from tests.test_items_spawn_joint_replay_real_locks import (
    _skip_if_required_artifacts_missing,
    _step_one_row,
)
from tests.test_combat_ownership_seed_guardrail_locks import _DEBUG_SHIELD_CANDIDATE_DTYPE
from tools.eval.validation_dtypes import COMPARE_DTYPE, SEED_DTYPE
from tools.eval.streaming_validation import _load_binding
from tools.extraction.char_registry import CHAR_BY_INTERNAL_ID
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views

_DEBUG_CONTACT_CLASSIFIED_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("contact_kind", "u1"),  # 0=BODY, 1=SHIELD
        ("hurtcap_id", "u1"),
        ("_pad0", "u1", (3,)),
        ("attacker_msid", "<u2"),
        ("attacker_action_frame", "<i2"),
        ("hitbox_x", "<f4"),
        ("hitbox_y", "<f4"),
        ("hitbox_z", "<f4"),
        ("hitbox_radius", "<f4"),
        ("hitbox_damage", "<f4"),
        ("hurtcap_ax", "<f4"),
        ("hurtcap_ay", "<f4"),
        ("hurtcap_az", "<f4"),
        ("hurtcap_bx", "<f4"),
        ("hurtcap_by", "<f4"),
        ("hurtcap_bz", "<f4"),
        ("hurtcap_radius", "<f4"),
        ("shield_x", "<f4"),
        ("shield_y", "<f4"),
        ("shield_z", "<f4"),
        ("shield_radius", "<f4"),
    ],
    align=False,
)


def _step_one_row_with_seed(dataset_path: Path, record: int, seed: np.ndarray) -> tuple[np.void, np.void]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    row = samples[record : record + 1]
    ref = row["ref_t1"][0]

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0], ref


def _rollout_window_with_seed(
    dataset_path: Path, start: int, stop: int, seed: np.ndarray
) -> tuple[np.void, np.void]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    _seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            binding.step_input(
                handle,
                prev_input_u8[record : record + 1, :input_stride].copy(),
                input_u8[record : record + 1, :input_stride].copy(),
            )
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0], samples["ref_t1"][stop]


def _step_one_row_with_seed_one_step(dataset_path: Path, record: int, seed: np.ndarray) -> tuple[np.void, np.void]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    row = samples[record : record + 1]
    ref = row["ref_t1"][0]

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0], ref


def _dataset_byte_views(ds):
    views = replay_buffer_byte_views(ds)
    return views.seed_t, views.prev_input_t, views.input_t


def _collect_contact_debug_for_row(dataset_path: Path, record: int) -> tuple[np.void, np.ndarray]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > record, f"replay too short for lock row: record={record}"
    row = samples[record : record + 1]

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)
    seed_bytes = seed_u8[record : record + 1, :seed_stride].copy()
    prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
    input_bytes = input_u8[record : record + 1, :input_stride].copy()

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        contacts_raw, count = binding.debug_combat_contacts_classified(handle, 0, 256)
        contacts = contacts_raw.reshape(-1).view(_DEBUG_CONTACT_CLASSIFIED_DTYPE)[:count].copy()
    finally:
        binding.destroy(handle)
    return row["seed_t"][0], contacts


def _step_one_row_with_inputs(
    dataset_path: Path, record: int, prev_input: np.ndarray, cur_input: np.ndarray
) -> tuple[np.void, np.void]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    row = samples[record : record + 1]
    ref = row["ref_t1"][0]

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(prev_input.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(cur_input.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0], ref


def _step_one_sample(ds, record: int) -> tuple[np.void, np.void, np.void]:
    samples = ds.rows
    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    return seed, ref, out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


def _run_dataset_rollout_records(
    ds_path: Path, *, start: int, records: tuple[int, ...], replay_frame_rng: bool = True
) -> dict[int, tuple[np.void, np.void]]:
    ds = load_replay_buffers(str(ds_path))
    samples = ds.rows
    assert int(samples.shape[0]) > max(records)
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)
    out_by_record: dict[int, tuple[np.void, np.void]] = {}
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, max(records) + 1):
            seed_frame_bytes = seed_u8[record : record + 1, :seed_stride].copy()
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            if replay_frame_rng:
                binding.step_input_replay_frame_rng(handle, seed_frame_bytes, prev_input_bytes, input_bytes)
            else:
                binding.step_input(handle, prev_input_bytes, input_bytes)
            if record in records:
                binding.write_compare(handle, out_compare_bytes)
                out_by_record[record] = (out_view[0].copy(), samples["ref_t1"][record].copy())
    finally:
        binding.destroy(handle)
    return out_by_record


def _action_id_enum_values(root: Path) -> dict[str, int]:
    text = (root / "src/action_ids.h").read_text()
    values: dict[str, int] = {}
    for match in re.finditer(r"^\s*(MSL_(?:ACT|SM)_[A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)", text, re.MULTILINE):
        values[match.group(1)] = int(match.group(2), 0)
    return values


def _commonfall_msids_for_action(root: Path, action_id: int) -> tuple[int, int, int]:
    values = _action_id_enum_values(root)
    families = (
        (
            ("MSL_ACT_FALL", "MSL_ACT_FALL_F", "MSL_ACT_FALL_B"),
            ("MSL_SM_FALL", "MSL_SM_FALL_F", "MSL_SM_FALL_B"),
        ),
        (
            ("MSL_ACT_FALL_AERIAL", "MSL_ACT_FALL_AERIAL_F", "MSL_ACT_FALL_AERIAL_B"),
            ("MSL_SM_FALL_AERIAL", "MSL_SM_FALL_AERIAL_F", "MSL_SM_FALL_AERIAL_B"),
        ),
        (
            ("MSL_ACT_FALL_SPECIAL", "MSL_ACT_FALL_SPECIAL_F", "MSL_ACT_FALL_SPECIAL_B"),
            ("MSL_SM_FALL_SPECIAL", "MSL_SM_FALL_SPECIAL_F", "MSL_SM_FALL_SPECIAL_B"),
        ),
    )
    for action_names, smid_names in families:
        if action_id in {values[name] for name in action_names}:
            return tuple(values[name] for name in smid_names)
    raise AssertionError(f"action_id {action_id} is not a CommonFall blend action")


def _commonfall_data_for_row(
    root: Path, seed: np.void, player: int
) -> tuple[float, float, float, tuple[int, int, int]]:
    common = json.loads((root / "data/common/ft_common_data.json").read_text())
    char_id = int(seed["char_id"][player])
    char_info = CHAR_BY_INTERNAL_ID.get(char_id)
    assert char_info is not None, f"unsupported char_id for CommonFall lock: {char_id}"
    char_data = json.loads((root / f"data/characters/{char_info.name}.json").read_text())
    msids = _commonfall_msids_for_action(root, int(seed["action_id"][player]))
    return (
        float(common["common_fall_blend_air_drift_threshold"]),
        float(common["common_fall_blend_lerp"]),
        float(char_data["air_drift_max"]),
        msids,
    )


def _commonfall_target_from_speed(
    speed_x: float,
    facing_dir: float,
    *,
    threshold: float,
    air_drift_max: float,
    msids: tuple[int, int, int],
) -> tuple[float, int]:
    assert air_drift_max > 0.0
    frac = float(speed_x) / float(air_drift_max)
    frac = max(-1.0, min(1.0, frac))
    abs_frac = abs(frac)
    neutral, forwards, backwards = msids
    if abs_frac <= threshold:
        return 0.0, neutral
    target = (abs_frac - threshold) / (1.0 - threshold)
    msid = forwards if frac * float(facing_dir) > 0.0 else backwards
    return target, msid


def _commonfall_tick(x4: float, target: float, lerp: float) -> float:
    return x4 + lerp * (target - x4)


def _debug_commonfall_precombat_state_from_rollout(
    dataset_path: Path, *, start: int, target_record: int, player: int
) -> tuple[float, int]:
    ds = load_replay_buffers(str(dataset_path))
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(
            handle, seed_u8[start : start + 1, :seed_stride].copy()
        )
        for record in range(start, target_record):
            binding.step_input_replay_frame_rng(
                handle,
                seed_u8[record : record + 1, :seed_stride].copy(),
                prev_input_u8[record : record + 1, :input_stride].copy(),
                input_u8[record : record + 1, :input_stride].copy(),
            )
        binding.apply_replay_frame_rng(
            handle,
            seed_u8[target_record : target_record + 1, :seed_stride].copy(),
        )
        binding.debug_step_input_pre_combat(
            handle,
            prev_input_u8[target_record : target_record + 1, :input_stride].copy(),
            input_u8[target_record : target_record + 1, :input_stride].copy(),
        )
        x4, msid = binding.debug_common_fall_blend_state(handle, 0, player)
    finally:
        binding.destroy(handle)
    return float(x4), int(msid)


def _debug_commonfall_precombat_state_from_seed(
    dataset_path: Path, *, record: int, player: int
) -> tuple[float, int]:
    ds = load_replay_buffers(str(dataset_path))
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_u8[record : record + 1, :seed_stride].copy())
        binding.debug_step_input_pre_combat(
            handle,
            prev_input_u8[record : record + 1, :input_stride].copy(),
            input_u8[record : record + 1, :input_stride].copy(),
        )
        x4, msid = binding.debug_common_fall_blend_state(handle, 0, player)
    finally:
        binding.destroy(handle)
    return float(x4), int(msid)


def _run_slp_rollout_records(
    slp_path: Path, *, start: int, records: tuple[int, ...], ports: list[int]
) -> dict[int, tuple[np.void, np.void]]:
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")
    ds = load_replay_buffers(
        slp_path=str(slp_path),
        ports=ports,
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.rows
    assert int(samples.shape[0]) > max(records)
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)
    out_by_record: dict[int, tuple[np.void, np.void]] = {}
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, max(records) + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            if record in records:
                binding.write_compare(handle, out_compare_bytes)
                out_by_record[record] = (out_view[0].copy(), samples["ref_t1"][record].copy())
    finally:
        binding.destroy(handle)
    return out_by_record


@pytest.mark.integration
def test_stm_jumpaerialf_tail_only_bair_rejects_false_body_and_keeps_real_hit() -> None:
    # STM rec773 is a false strong BackAir BODY contact against only Fox JumpAerialF cap12/tail.
    # Runtime rejects that tail-only selected-HitCapsule source without promoting JumpAerialF/B into
    # the global dynamic-pose data index; rec774 is the adjacent deeper contact with low/body
    # hurtcaps and must remain a real hit.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/chara/ftFox/ftFox_AttackAir.c::{ftCo_8009DD94,ftCo_8009E318}
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
    # data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/pokemon_stadium_recent/SweatyThisMallard.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 773)
    assert int(seed["action_id"][0]) == 67  # AttackAirB.
    assert int(seed["action_id"][1]) == 27  # JumpAerialF.
    assert int(ref["action_id"][1]) == 27
    assert int(out["action_id"][1]) == int(ref["action_id"][1])
    assert int(out["hitlag"][1]) == int(ref["hitlag"][1]) == 0
    assert int(out["hitstun"][1]) == int(ref["hitstun"][1]) == 0
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]))

    seed, out, ref = _step_one_row(dataset_path, 774)
    assert int(seed["action_id"][0]) == 67
    assert int(seed["action_id"][1]) == 27
    assert int(ref["action_id"][1]) == 86  # DamageFlyLw from the real adjacent BODY contact.
    assert int(out["action_id"][1]) == int(ref["action_id"][1])
    assert int(out["hitlag"][1]) == int(ref["hitlag"][1])
    assert int(out["hitstun"][1]) == int(ref["hitstun"][1])
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]))


@pytest.mark.integration
def test_stm_pokemon_stadium_x44_body_gap_owners_are_payload_bounded() -> None:
    # Pokemon Stadium x44 BODY gap owner:
    # Stadium's x34_scale.z / x44_mtx lane reaches ftCommon_8007F804 before ftColl BODY collision.
    # Runtime admits only the extracted payload/cap gaps that the reduced matrix path misses:
    # strong DAir high/tail BODY and AttackHi3 hb1 vs part-18 tail. Adjacent shallower frames must
    # remain no-hit, proving this is not a broad Stadium tolerance.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_80068E64,Fighter_UpdateModelScale}
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw/events.ftCo_SM_AttackHi3
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/pokemon_stadium_recent/SweatyThisMallard.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    for record, defender, expected_action in ((976, 1, 14), (4224, 1, 26), (6646, 1, 25)):
        seed, out, ref = _step_one_row(dataset_path, record)
        assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == expected_action
        assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 0
        assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))

    for record, defender, expected_action, expected_hitlag in (
        (977, 1, 87, 7),
        (4225, 1, 88, 7),
        (6647, 1, 90, 6),
    ):
        seed, out, ref = _step_one_row(dataset_path, record)
        assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == expected_action
        assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == expected_hitlag
        assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender])
        assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))

    ds = load_replay_buffers(str(dataset_path))
    for record, defender, no_x44_action in ((4225, 1, 26), (6647, 1, 25)):
        non_stadium_seed = ds.rows[record : record + 1]["seed_t"].copy()
        non_stadium_seed["stage_id"][0] = np.uint32(32)  # Final Destination.
        out, _ref = _step_one_row_with_seed(dataset_path, record, non_stadium_seed)
        assert int(out["action_id"][defender]) == no_x44_action
        assert int(out["hitlag"][defender]) == 0


@pytest.mark.integration
def test_strong_dair_attacklw4_medium_sibling_owns_damage_height_teg_1532() -> None:
    # Strong DAir selected-height owner:
    # TEG 1532 is a grounded Fox AttackLw4 victim struck by Falco's strong AttackAirLw pair.
    # Runtime geometry sees hb0 overlap cap2/high first, but the same source collision pass also
    # has the authored same-group hb1 strong-DAir sibling overlapping cap1/medium. Source
    # ftColl_8007A06C consumes the selected DmgLog hurt height as DamageFlyN.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8,ftColl_8007A06C}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/sheik/TornEnchantingGiraffe.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 1532)
    assert int(seed["action_id"][0]) == 64  # AttackLw4.
    assert int(seed["action_id"][1]) == 69  # AttackAirLw.
    assert int(ref["action_id"][0]) == 88  # DamageFlyN.
    assert int(out["action_id"][0]) == int(ref["action_id"][0])
    assert int(out["animation_index"][0]) == int(ref["animation_index"][0])
    assert int(out["hitlag"][0]) == int(ref["hitlag"][0]) == 6
    assert int(out["hitstun"][0]) == int(ref["hitstun"][0])
    assert float(out["percent"][0]) == pytest.approx(float(ref["percent"][0]))


@pytest.mark.integration
def test_strong_dair_attacklw4_high_cap_stays_without_medium_sibling_teg_1532_negative() -> None:
    # Adjacent negative for the selected-height owner above. When the same-group hb1 medium-cap
    # sibling is absent, the live hb0/cap2 high source remains authoritative and enters DamageFlyHi.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/sheik/TornEnchantingGiraffe.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    binding = _load_binding()
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[1532:1533]
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(
        row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, input_stride)
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        binding.debug_set_hitbox_world(handle, 0, 1, 1, 0.0, 0.0, 0.0, 0.0, 0.0, 0)
        binding.debug_combat_resolve(handle)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][0]) == 87  # DamageFlyHi from hb0/cap2 high.
    assert int(out["animation_index"][0]) == 177


@pytest.mark.integration
def test_tvr_specialhi_launch_x44_body_gap_owner_is_stage_and_payload_bounded() -> None:
    # TVR rec12278 closes the Pokemon Stadium x44 BODY residual for generated up-special launch:
    # authored SpecialHi hb0 (16 damage, angle 80, kbg 60, bkb 80) reaches the airborne root cap
    # through ftCommon_8007F804's Stadium x44 matrix. The same seed on a non-Stadium stage remains
    # no-hit, proving this is the x44 source lane rather than a generic SpecialHi/root shortcut.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_80068E64,Fighter_UpdateModelScale}
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHi_Enter,
    #   ftFx_SpecialAirHi_Enter}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/pokemon_stadium_recent/"
        / "ThisVioletRaccoon.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 12278)
    defender = 0
    assert int(seed["action_id"][1]) == 356  # SpecialAirHi launch.
    assert int(seed["action_id"][defender]) == 366  # SpecialAirLwLoop before Turn publication.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 90
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 8
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))

    ds = load_replay_buffers(str(dataset_path))
    non_stadium_seed = ds.rows[12278:12279]["seed_t"].copy()
    non_stadium_seed["stage_id"][0] = np.uint32(32)  # Final Destination.
    out, _ref = _step_one_row_with_seed(dataset_path, 12278, non_stadium_seed)
    assert int(out["action_id"][defender]) == 369  # No x44 BODY hit.
    assert int(out["hitlag"][defender]) == 0


@pytest.mark.integration
def test_tvr_specialhi_launch_x44_body_gap_waits_for_aerial_reflector_turn_rollout() -> None:
    # Full-rollout TVR rec12277 has the same generated SpecialAirHi launch hb0 near the defender
    # root, but the victim is still ftFx_MS_SpecialAirLwLoop. The bounded Stadium x44 bridge must wait
    # until rec12278, where the source motion state is ftFx_MS_SpecialAirLwTurn. This is a generated
    # Fox/Falco SpecialLw motion-state distinction, not a replay-row or character-pair predicate.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    #   ftFx_SpecialAirLwLoop_IASA,ftFx_SpecialAirLwLoop_Coll,ftFx_SpecialAirLwTurn_Coll}
    # refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/pokemon_stadium_recent/"
        / "ThisVioletRaccoon.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    defender = 0
    attacker = 1
    assert int(ds.rows[12277]["ref_t1"]["action_id"][defender]) == 366  # SpecialAirLwLoop.
    assert int(ds.rows[12278]["seed_t"]["action_id"][defender]) == 366
    assert int(ds.rows[12278]["ref_t1"]["action_id"][attacker]) == 356  # SpecialAirHi launch.

    rows = _run_dataset_rollout_records(dataset_path, start=0, records=(12277, 12278))
    out_12277, ref_12277 = rows[12277]
    assert int(out_12277["action_id"][defender]) == int(ref_12277["action_id"][defender]) == 366
    assert int(out_12277["hitlag"][defender]) == int(ref_12277["hitlag"][defender]) == 0
    assert int(out_12277["last_hit_by"][defender]) == int(ref_12277["last_hit_by"][defender])

    out_12278, ref_12278 = rows[12278]
    assert int(out_12278["action_id"][defender]) == int(ref_12278["action_id"][defender]) == 90
    assert int(out_12278["hitlag"][defender]) == int(ref_12278["hitlag"][defender]) == 8
    assert float(out_12278["percent"][defender]) == pytest.approx(float(ref_12278["percent"][defender]))


@pytest.mark.integration
def test_attackairlw_sustained_tiplog_contact_defers_grounded_dash_damage_selfplay_181413() -> None:
    # Self-play 181413 rec316 is a source-general fighter BODY tip-log boundary:
    # - p0 Fox AttackAirLw has a sustained HitCapsule whose exact lbColl_80006E58 matrix overlap
    #   against grounded p1 Dash is below common x7A8.
    # - ftColl_80076ED8 therefore takes checkTipLog/inlineB1, starting victim hitlag and attribution
    #   without percent/KB/damage-state entry.
    # - rec318 is the adjacent negative: the later deeper contact is a full BODY damage hit.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,checkTipLog,inlineB1}
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = load_replay_buffers(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )

    attacker = 0
    defender = 1
    seed, ref, out = _step_one_sample(ds, 316)
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw.
    assert int(seed["action_id"][defender]) == 18  # Turn.
    assert int(ref["action_id"][defender]) == 20  # Dash.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 3
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 0
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))
    assert int(out["instance_hit_by"][defender]) == int(ref["instance_hit_by"][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 0

    seed, ref, out = _step_one_sample(ds, 318)
    assert int(seed["action_id"][defender]) == 20  # Dash.
    assert int(ref["action_id"][defender]) == 76  # DamageHi2.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 3
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 3
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 17
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
def test_attackairlw_primary_hitbox_tiny_overlap_stays_full_body_damage_prh() -> None:
    # Negative boundary for the retained AttackAirLw BODY tip-log lane:
    # PRH rec5032 is Fox AttackAirLw primary hitbox slot 0 against grounded Wait. The current
    # matrix-radius overlap is tiny, but vanilla takes full BODY damage on hb0; only non-primary
    # limb slots consume the retained matrix-overlap tip-log reconstruction until the exact
    # lbColl_80006E58/victims_2 scalar owner is ported for every slot.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,checkTipLog,inlineB1}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/PositiveRevolvingHyena.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[5032]["seed_t"]
    out, ref = _step_one_row_with_seed_one_step(dataset_path, 5032, seed)

    attacker = 1
    defender = 0
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw.
    assert int(seed["action_id"][defender]) == 14  # Wait.
    assert int(ref["action_id"][defender]) == 80  # DamageN3.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 27
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
def test_attackairlw_commonfall_matrix_only_positive_rejects_false_body_ewt_1019() -> None:
    # ElatedWearyTermite rec1019 is a FoD rollout false hit: p0 Fox AttackAirLw hb1 sweeps near
    # p1 Falco Fall. The ordinary world capsule misses, and Dolphin lbColl probes show vanilla
    # lbColl_8000805C/lbColl_80006E58 also rejects the live Fall JObj pose. MSL's generated
    # state-age Fall pose is not source-complete enough to create a matrix-only BODY positive; it
    # may still reject contacts when the world geometry already overlaps.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # reports/triage/grape_item05_ewt1019_dolphin_collision_probe/
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[1019]["seed_t"]
    out, ref = _step_one_row_with_seed_one_step(dataset_path, 1019, seed)

    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw.
    assert int(seed["action_id"][defender]) == 29  # Fall.
    assert int(ref["action_id"][defender]) == 29  # Fall; no BODY hit.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 0
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 0
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 0
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))

    rollout, rollout_ref = _rollout_window_with_seed(dataset_path, 0, 1019, ds.rows[0]["seed_t"])
    assert int(rollout["action_id"][defender]) == int(rollout_ref["action_id"][defender]) == 29
    assert int(rollout["hitlag"][attacker]) == int(rollout_ref["hitlag"][attacker]) == 0
    assert int(rollout["hitlag"][defender]) == int(rollout_ref["hitlag"][defender]) == 0
    assert int(rollout["hitstun"][defender]) == int(rollout_ref["hitstun"][defender]) == 0


@pytest.mark.integration
def test_commonfall_transn_local_blend_rejects_false_body_dcc_5573() -> None:
    # CommonFall collision-pose negative:
    # DCC:5573 is a tight no-hit boundary after a Fall animation loop. Dolphin lbColl probes reject
    # the p0 AttackS BODY candidate against p1's live Fall JObj pose, so the simulator must rebuild
    # the ftAnim_8006FE9C local-SRT owner without blending pre-TransN ancestors or stale seed pose.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
    #   ftCo_Fall_Anim_Inner,ftCo_800CC988}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "replays/validation/aggregate_recent/DistinctCaringCobra.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    rows = _run_dataset_rollout_records(dataset_path, start=0, records=(5573,), replay_frame_rng=True)
    out, ref = rows[5573]
    defender = 1
    assert int(ref["action_id"][defender]) == 29  # Fall, no BODY hit.
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent"):
        assert out[field][defender] == pytest.approx(ref[field][defender])


@pytest.mark.integration
def test_commonfall_entry_tick_hidden_lane_iat_6425() -> None:
    # Direct phase lock for the hidden CommonFall recurrence: after rolling through the
    # Fall entry frame, the next pre-combat phase must have consumed the entry recurrence
    # plus the current ftCo_Fall_Anim_Inner tick before BODY selection. This checks
    # mv.co.fall.x4/smid directly rather than relying on downstream hit admission.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
    #   ftCo_Fall_Anim,ftCo_Fall_Anim_Inner,ftCo_800CC988}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    player = 1
    record = 6425
    seed = ds.rows[record]["seed_t"]
    enum_values = _action_id_enum_values(root)
    assert int(seed["action_id"][player]) == enum_values["MSL_ACT_FALL"]
    assert int(seed["action_frame"][player]) == 0

    threshold, lerp, air_drift_max, msids = _commonfall_data_for_row(root, seed, player)
    target, expected_msid = _commonfall_target_from_speed(
        float(seed["speed_air_x_self"][player]),
        float(seed["facing_dir1"][player]),
        threshold=threshold,
        air_drift_max=air_drift_max,
        msids=msids,
    )
    expected_x4 = _commonfall_tick(_commonfall_tick(0.0, target, lerp), target, lerp)

    x4, msid = _debug_commonfall_precombat_state_from_rollout(
        dataset_path, start=0, target_record=record, player=player
    )
    assert msid == expected_msid
    assert x4 == pytest.approx(expected_x4, abs=1.0e-6)


@pytest.mark.integration
def test_commonfall_seeded_fall_frame1_does_not_apply_entry_tick_iat_6426() -> None:
    # Adjacent seeded Fall control: reseed reconstructs the real hidden x4 from action_frame,
    # so a frame-1 Fall row must consume only the current Anim tick before BODY selection.
    # Over-applying the entry recurrence here would advance mv.co.fall.x4 one tick too far.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim_Inner
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    player = 1
    record = 6426
    seed = ds.rows[record]["seed_t"]
    enum_values = _action_id_enum_values(root)
    assert int(seed["action_id"][player]) == enum_values["MSL_ACT_FALL"]
    assert int(seed["action_frame"][player]) == 1

    threshold, lerp, air_drift_max, msids = _commonfall_data_for_row(root, seed, player)
    target, expected_msid = _commonfall_target_from_speed(
        float(seed["speed_air_x_self"][player]),
        float(seed["facing_dir1"][player]),
        threshold=threshold,
        air_drift_max=air_drift_max,
        msids=msids,
    )
    seeded_x4 = _commonfall_tick(0.0, target, lerp)
    expected_x4 = _commonfall_tick(seeded_x4, target, lerp)

    x4, msid = _debug_commonfall_precombat_state_from_seed(
        dataset_path, record=record, player=player
    )
    assert msid == expected_msid
    assert x4 == pytest.approx(expected_x4, abs=1.0e-6)


@pytest.mark.integration
def test_commonfall_entry_tick_admits_attackairhi_iat_6428() -> None:
    # Adjacent positive for the same CommonFall owner: IAT:6428 is reached through a free-running
    # Fall entry, and Dolphin lbColl probes show the contact-phase Fall JObj consuming the hidden
    # entry-frame x4 recurrence before p0 AttackAirHi hb0 hits p1 cap12. This guards the source
    # phase owner, not a replay-derived scalar threshold.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
    #   ftCo_Fall_Anim_Inner,ftCo_800CC988}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    rows = _run_dataset_rollout_records(dataset_path, start=0, records=(6428,), replay_frame_rng=True)
    out, ref = rows[6428]
    defender = 1
    assert int(ref["action_id"][defender]) == 84  # DamageFlyHi.
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent"):
        assert out[field][defender] == pytest.approx(ref[field][defender])


@pytest.mark.integration
def test_marth_uair_admits_commonfall_blended_body_pose_ldg_4539() -> None:
    # Common Fall live blend BODY positive:
    # - p1 Marth AttackAirHi hb0 is already extracted exactly to Dolphin's live hit center.
    # - Vanilla accepts p0 Fox hurtcap 9 only after ftCo_Fall_Anim_Inner blends Fall toward FallB
    #   via mv.co.fall.x4; the neutral state-age Fall pose misses.
    # - This lock proves the owner is the defender-side CommonFall/FallF/FallB live JObj blend,
    #   not a Marth sword hitbox adjustment or row-local combat exception.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
    #   ftCo_Fall_Anim_Inner,ftCo_800CC988}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # reports/triage/marth_burndown_20260613T093537Z/batch15_ldg4539_collision_probe/
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "replays/validation/marth/LoudDullGoat.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[4539]["seed_t"]
    out, ref = _step_one_row_with_seed_one_step(dataset_path, 4539, seed)

    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 29  # Fall.
    assert int(seed["animation_index"][defender]) == 20  # ftCo_SM_Fall.
    assert int(seed["action_frame"][defender]) == 3
    assert int(seed["action_id"][attacker]) == 68  # AttackAirHi.
    assert int(ref["action_id"][defender]) == 90  # DamageFlyTop.
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent"):
        assert out[field][defender] == pytest.approx(ref[field][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 7


@pytest.mark.integration
def test_commonfall_blend_positive_requires_drift_target_ldg_4539() -> None:
    # Adjacent source-completion negative for the same CommonFall owner: when self_vel.x is inside
    # p_ftCommonData->x444, ftCo_Fall_Anim_Inner filters x4 toward 0 and ftCo_800CC988 does not
    # publish the FallB leg pose that admits LDG:4539.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim_Inner
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / "replays/validation/marth/LoudDullGoat.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[4539]["seed_t"].copy()
    defender = 0
    seed["speed_air_x_self"][defender] = np.float32(0.0)
    out, _ref = _step_one_row_with_seed_one_step(dataset_path, 4539, seed)

    assert int(seed["action_id"][defender]) == 29  # Fall.
    assert int(out["action_id"][defender]) == 29
    assert int(out["hitlag"][defender]) == 0
    assert int(out["hitstun"][defender]) == 0


@pytest.mark.integration
def test_commonfall_entry_matrix_positive_still_admits_body_dcc_5574() -> None:
    # Boundary control for the CommonFall matrix-only BODY veto: Fall entry is still eligible for
    # source matrix positives because Fighter_ChangeMotionState has just initialized the JObj chain.
    # DistinctCaringCobra rec5574 is a direct frame-1 Fall BODY admission from p0 AttackAirF.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root / "replays/validation/aggregate_recent/DistinctCaringCobra.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[5574]["seed_t"]
    out, ref = _step_one_row_with_seed_one_step(dataset_path, 5574, seed)

    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 50  # AttackAirF.
    assert int(seed["action_id"][defender]) == 29  # Fall.
    assert int(seed["action_frame"][defender]) == 1
    assert int(ref["action_id"][defender]) == 90  # DamageFlyTop.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 4
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 4
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 38
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
def test_grounded_attack_restart_clears_sustained_hitcapsule_latch_selfplay_181413() -> None:
    # Same-action grounded Attack restart HitCapsule clear:
    # - p0 Fox re-enters AttackHi3 without a Slippi action_id change; action_frame rewinds from
    #   22 to 1 while the old same-source victim attribution remains visible on p1 DamageFlyN.
    # - Source still ran Fighter_ChangeMotionState -> ftColl_8007AFF8 and the fresh
    #   ftAction_8007121C create edge owns lbColl_80008440 clear/copy, so BODY attribution alone
    #   must not re-materialize the old victims_1 latch for the new AttackHi3.
    # - rec1348 is the adjacent negative: the fresh hitboxes exist but vanilla has not taken the
    #   new BODY hit yet. rec1349 is the positive: the same fresh hitboxes enter DamageFlyTop.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AFF8,ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_8000ACFC}
    # MSLMSO01: MSL_MS_CLASS_GROUNDED_ATTACK.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    if not slp_path.exists():
        pytest.skip(f"missing local replay: {slp_path}")

    ds = load_replay_buffers(
        slp_path=str(slp_path),
        ports=[1, 2],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    start = 0
    stop = 1349
    attacker = 0
    defender = 1
    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)

            if record == 1348:
                out = out_view[0].copy()
                ref = samples["ref_t1"][record]
                assert int(samples["seed_t"][record]["action_id"][attacker]) == 56  # AttackHi3
                assert int(ref["action_id"][defender]) == 88  # DamageFlyN
                assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
                assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 0
                assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender])
                assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))

        out = out_view[0].copy()
        ref = samples["ref_t1"][stop]
        assert int(samples["seed_t"][stop]["action_id"][attacker]) == 56  # AttackHi3
        assert int(samples["seed_t"][stop]["action_frame"][attacker]) == 5
        assert int(ref["action_id"][defender]) == 90  # DamageFlyTop
        for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
            assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
        assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_damagefly_terminal_fall_entry_blocks_fresh_enable_edge_body_selfplay_181413() -> None:
    # Self-play 181413 rec2361 is a terminal DamageFlyTop -> Fall IASA row. The attacker has a
    # freshly enabled AttackHi4 BODY capsule, but source order does not let that create-edge
    # HitCapsule consume the post-IASA Fall target until the next collision frame. The rollout lock
    # proves the terminal row stays in Fall with no immediate rehit, while the following frame still
    # admits the real BODY hit instead of suppressing the attack family.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    #   ftCo_8008F744,ftCo_DamageFly_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    slp_path = root / "replays/validation/aggregate_recent/Game_20260514T181413.slpz"
    rows = _run_slp_rollout_records(slp_path, start=0, records=(2361, 2362), ports=[1, 2])
    defender = 0

    out_2361, ref_2361 = rows[2361]
    assert int(ref_2361["action_id"][defender]) == 29  # Fall.
    assert int(out_2361["action_id"][defender]) == int(ref_2361["action_id"][defender])
    assert int(out_2361["hitlag"][defender]) == int(ref_2361["hitlag"][defender]) == 0
    assert int(out_2361["hitstun"][defender]) == int(ref_2361["hitstun"][defender]) == 0
    assert float(out_2361["percent"][defender]) == pytest.approx(
        float(ref_2361["percent"][defender]), abs=1e-6
    )

    out_2362, ref_2362 = rows[2362]
    assert int(ref_2362["action_id"][defender]) == 90  # DamageFlyTop.
    assert int(out_2362["action_id"][defender]) == int(ref_2362["action_id"][defender])
    assert int(out_2362["hitlag"][defender]) == int(ref_2362["hitlag"][defender]) == 9
    assert float(out_2362["percent"][defender]) > float(out_2361["percent"][defender])


@pytest.mark.integration
def test_damagefly_terminal_fall_entry_does_not_block_same_frame_shine_entry_ppa() -> None:
    # Negative boundary for terminal DamageFly/DamageFall -> Fall create-edge suppression:
    # PriceyPartialAlbatross rec2656 has p1 enter grounded SpecialLwStart from Squat on the same
    # frame p0 is on the terminal DamageFlyTop hitstun tick. Vanilla admits the fresh Shine BODY
    # hit before p0 can settle into Fall. The retained suppression is therefore limited to
    # same-action attack create edges, not all newly-enabled BODY capsules near terminal damage.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwStart_Action
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/PriceyPartialAlbatross.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 2656)
    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 90  # DamageFlyTop.
    assert int(seed["hitstun"][defender]) == 1
    assert int(seed["action_id"][attacker]) == 39  # Squat.
    assert int(ref["action_id"][attacker]) == 360  # Fox SpecialLwStart.
    assert int(ref["action_id"][defender]) == 90  # DamageFlyTop re-entry from Shine.
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"defender field={field}"
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 5
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
def test_grounded_attack_restart_clear_applies_to_attackdash_tch() -> None:
    # Grounded Attack* same-action restart clear outside the self-play Fox AttackHi3 row:
    # TubbyCurlyHerring rec1817 is Fox AttackDash against Falco DamageFlyN. The attacker remains
    # in AttackDash, but source has created a fresh grounded Attack* HitCapsule list for this
    # action instance, so same-source stale BODY attribution from earlier rows cannot suppress the
    # new hit. This proves the retained owner is the MSLMSO01 grounded-attack class boundary, not a
    # Fox AttackHi3 replay slice.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AFF8,ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_8000ACFC}
    # MSLMSO01: MSL_MS_CLASS_GROUNDED_ATTACK.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing validation dataset: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 1817)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 50  # AttackDash.
    assert int(seed["action_frame"][attacker]) == 4
    assert int(seed["action_id"][defender]) == 70  # FallSpecial.
    assert int(ref["action_id"][defender]) == 77  # DamageHi3.
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]))


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "defender"),
    [
        ("DistinctCaringCobra.slpz", 9272, 1),
        ("TubbyCurlyHerring.slpz", 3186, 1),
    ],
)
def test_enable_edge_tiplog_phantom_rows_do_not_enter_damage(dataset_name: str, record: int, defender: int) -> None:
    # Enable-edge BODY phantom/tip-log lock:
    # - The selected HitCapsule overlaps by less than p_ftCommonData->x7A8.
    # - Vanilla starts hitlag through ftColl_80076ED8's tip-log lane but does not enter a damage
    #   motion state or write hitstun.
    # - The runtime branch is decomp-shaped and uses the collision matrix helper, not a replay row
    #   or post-admission admission bridge.
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80076ED8,ftColl_8007AD18}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, record)
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_specialairsstart_pre_anim_pose_rejects_dtilt_body_cnm_5292() -> None:
    # Fox/Falco Side-B Start collision-pose ownership:
    # - BODY collision consumes the same frame's pre-Anim JObj pose on SpecialAirSStart startup.
    # - The generic post-Anim pose plus x58/x4C sweep admits a false Falco dtilt BODY hit against
    #   Fox Side-B startup low hurtcaps on CNM:5292; vanilla keeps Fox in SpecialAirSStart with no
    #   hitlag/hitstun.
    # - Adjacent real Falco BODY hits on the same flagless parts stay admitted by the row-level
    #   validation diff; this is not a generic attacker model-scale or part-id filter.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    #   ftFx_SpecialSStart_Anim,ftFx_SpecialAirSStart_Anim,ftFx_SpecialSStart_Coll,
    #   ftFx_SpecialAirSStart_Coll}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/yoshis_story_recent/CheeryNumbMonkey.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 5292)
    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 350  # SpecialAirSStart
    assert int(seed["action_id"][attacker]) == 57  # AttackLw3
    assert int(seed["action_frame"][attacker]) == 7
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_id", "percent"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"defender field={field}"
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_id"):
        assert int(out[field][attacker]) == int(ref[field][attacker]), f"attacker field={field}"


@pytest.mark.integration
def test_damageflyhi_terminal_aobj_pose_selects_high_hurtcap_dcc_8565() -> None:
    # DamageFly terminal AObj collision pose:
    # - ftCo_DamageFly_Anim keeps the victim in active hitstun after the non-looping AObj reaches
    #   end_frame.
    # - HSD_AObjInterpretAnim marks those stopped JObjs AOBJ_NO_ANIM, while lb_8000B1CC still
    #   consumes their final live local SRT for BODY hurtcaps in ftColl_80078C70.
    # - DCC:8565 is Falco DamageFlyHi at end_frame=29; the high hurtcap must use that stopped
    #   terminal AObj pose so Fox AttackAirB selects the strong high hitbox, not the weak mid one.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Anim
    # refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    # refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/DistinctCaringCobra.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 8565)
    defender = 1
    assert int(seed["action_id"][defender]) == 87  # ftCo_MS_DamageFlyHi
    assert int(seed["animation_index"][defender]) == 177  # ftCo_SM_DamageFlyHi
    assert int(seed["action_frame"][defender]) == 29
    assert int(ref["action_id"][defender]) == 87
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 8
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 57
    assert int(out["percent"][defender]) == int(ref["percent"][defender])


@pytest.mark.integration
def test_damageflyhi_terminal_aobj_pose_does_not_pre_admit_dcc_8564() -> None:
    # Negative neighbor for the stopped-AObj terminal pose: the immediately preceding DCC row has
    # the same DamageFlyHi terminal victim but no accepted BODY hit yet. The extractor/runtime
    # change must not become a broad DamageFlyHi hit admission shortcut.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/DistinctCaringCobra.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 8564)
    defender = 1
    assert int(seed["action_id"][defender]) == 87
    assert int(seed["animation_index"][defender]) == 177
    assert int(seed["action_frame"][defender]) == 29
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_rebirth_fall_x1994_seed_expires_hurtbox_state_gat_9068() -> None:
    # RebirthWait -> Fall x1994 seed owner:
    # - RebirthWait_Anim / IASA call ftColl_8007B7A4(..., p_ftCommonData->x5D8) before Fall.
    # - GAT:9068 is the terminal seeded x1994=1 frame several actions after RebirthWait -> Fall.
    # - Fighter_8006A360 must decrement that timer and clear x198C before post-frame compare;
    #   stale-carrying the merged Slippi hurtbox_state leaves p1 invincible one frame too long.
    # refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_RebirthWait_{Anim,IASA}
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 9068)
    defender = 1
    assert int(seed["action_id"][defender]) == 39  # ftCo_MS_Squat
    assert int(seed["hurtbox_state"][defender]) == 1
    assert int(seed["colanim_hit_status_x198c"][defender]) == 1
    assert int(seed["colanim_timer_x1994"][defender]) == 1
    assert int(seed["colanim_rebirth_fall_x1994_seed"][defender]) == 1
    assert int(out["hurtbox_state"][defender]) == int(ref["hurtbox_state"][defender]) == 0


@pytest.mark.integration
def test_rebirth_fall_x1994_seed_requires_rebirth_source_flag_gat_9068() -> None:
    # Boundary guard: a nonzero x1994 timer is not trusted as generic gameplay state unless the
    # replay-history lane proves the RebirthWait -> Fall owner. This keeps the fix from becoming a
    # broad "timer means clear hurtbox_state" shortcut for unrelated x1994 sources.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[9068:9069]["seed_t"].copy()
    defender = 1
    assert int(seed[0]["colanim_rebirth_fall_x1994_seed"][defender]) == 1
    seed[0]["colanim_rebirth_fall_x1994_seed"][defender] = np.uint8(0)
    out, ref = _step_one_row_with_seed(dataset_path, 9068, seed)
    assert int(ref["hurtbox_state"][defender]) == 0
    assert int(out["hurtbox_state"][defender]) == 1


@pytest.mark.integration
def test_rebirth_fall_x1994_seed_fixes_gat_9063_rollout_window() -> None:
    # Rollout-real lock for the disruptive F08b GAT cluster:
    # starting at GAT:9063, p1 is still in the RebirthWait -> Fall invincibility window. The hidden
    # x1994 timer must expire at GAT:9068, otherwise p1 remains invincible and later BODY/contact
    # selection diverges into the high-scoring rollout blast radius.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    start = 9063
    stop = 9125
    defender = 1
    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_view[0].copy()
            ref = samples["ref_t1"][record]
            for field in ("action_id", "animation_index", "hitlag", "hitstun", "hurtbox_state"):
                assert int(out[field][defender]) == int(ref[field][defender]), f"record={record} field={field}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardreflect_peer_kneebend_nudge_requires_frame_start_grounded_tbk_1426() -> None:
    # Fighter_8006A360 runs ftCommon_8007E0E4 per fighter after that fighter's Anim callback.
    # On TBK:1426 p0 GuardReflect still sees p1's frame-start grounded KneeBend pushbox before
    # p1's later KneeBend_Anim enters JumpF. Mutating the peer to start airborne must remove that
    # x450 player-nudge lane rather than applying it from a broad current-action shortcut.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007E0E4,ftCommon_8007DD7C}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[1426:1427]["seed_t"].copy()
    assert int(seed[0]["action_id"][0]) == 182  # GuardReflect
    assert int(seed[0]["action_id"][1]) == 24  # KneeBend
    assert int(seed[0]["on_ground"][1]) == 1

    out, ref = _step_one_row_with_seed(dataset_path, 1426, seed)
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == 182
    assert float(out["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=3e-5)

    seed[0]["on_ground"][1] = np.uint8(0)
    seed[0]["ground_id"][1] = np.uint16(0xFFFF)
    out_air, _ = _step_one_row_with_seed(dataset_path, 1426, seed)
    assert float(out_air["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]) + 0.3, abs=3e-5)


@pytest.mark.integration
def test_guardreflect_final_x14_shine_body_rollout_rejects_broad_shield_extent_tbk_1403() -> None:
    # Rollout-real lock for the TBK F08b disruptive row:
    # - p0's GuardReflect final-x14 no-submotion ShieldDesc is near the p1 shine hitbox rim.
    # - The broad ShieldDesc extent/model-scale proxy falsely turns the row into GuardSetOff.
    # - Source-shaped final-x14 fighter-vs-fighter shield admission must reject that shield
    #   candidate so the BODY contact enters DamageFlyTop.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_80092450}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    start = 1403
    stop = 1427
    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)

        out = out_view[0].copy()
        ref = samples["ref_t1"][stop]
        assert int(ref["action_id"][0]) == 90  # DamageFlyTop
        for field in ("action_id", "animation_index", "hitlag", "hitstun"):
            assert int(out[field][0]) == int(ref[field][0]), f"field={field}"
        assert float(out["percent"][0]) == pytest.approx(float(ref["percent"][0]), abs=1e-6)
        assert float(out["shield_hp"][0]) == pytest.approx(float(ref["shield_hp"][0]), abs=1e-6)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardreflect_final_x14_shine_precombat_shield_candidate_is_rejected_tbk_1427() -> None:
    # Negative boundary for the same owner: at the collision snapshot, the p1 shine hitbox is a
    # valid BODY candidate but must not be accepted as a GuardReflect shield hit.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    start = 1403
    target = 1427
    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, target):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)

        prev_input_bytes = prev_input_u8[target : target + 1, :input_stride].copy()
        input_bytes = input_u8[target : target + 1, :input_stride].copy()
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        raw_cand, count_cand = binding.debug_shield_candidate_decisions(handle, 0, 128)
    finally:
        binding.destroy(handle)

    cand = raw_cand.reshape(-1).view(_DEBUG_SHIELD_CANDIDATE_DTYPE)[:count_cand]
    accepted = [
        c
        for c in cand
        if int(c["attacker"]) == 1
        and int(c["defender"]) == 0
        and int(c["hitbox_id"]) == 0
        and int(c["reject_reason"]) == 0
        and int(c["overlap_shield"]) == 1
    ]
    assert not accepted


@pytest.mark.integration
def test_guardreflect_expired_x14_attackhi3_enable_edge_reaches_guardsetoff_tbk_3585() -> None:
    # Rollout-real positive for the expired-x14 no-submotion ShieldDesc create-edge lane:
    # - p1 is a GuardReflect no-submotion snapshot whose x14 has expired and whose ShieldDesc is
    #   replay-visible before collision.
    # - p0 AttackHi3 creates fresh HitCapsules this frame; ftColl_8007AD18 forwards ShieldDesc.size
    #   to the lbColl overlap helper for that create-edge capsule.
    # - Persistent capsules near the same GuardReflect rim stay covered by the TBK:1427 shine
    #   negative above, which must not borrow this lane.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_80092450}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80078C70}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/"
        "TreasuredBackKangaroo.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    start = 3585
    stop = 3610
    attacker = 0
    defender = 1
    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)

        out = out_view[0].copy()
        ref = samples["ref_t1"][stop]
        assert int(samples["seed_t"][stop]["action_id"][attacker]) == 56  # AttackHi3
        assert int(samples["seed_t"][stop]["action_id"][defender]) == 182  # GuardReflect
        assert int(ref["action_id"][defender]) == 181  # GuardSetOff
        for field in ("action_id", "animation_index", "hitlag", "hitstun"):
            assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
        assert float(out["shield_hp"][defender]) == pytest.approx(
            float(ref["shield_hp"][defender]), abs=1e-5
        )
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardreflect_expired_x14_no_submotion_body_uses_guardon_hurtcaps_gat_11085() -> None:
    # Expired-x14 GuardReflect no-submotion BODY handoff:
    # - ftCo_GuardReflect_Anim calls ftCo_80093BC0 before the fighter-vs-fighter collision pass.
    # - Once mv.co.guard.x14 expires, ftCo_80093BC0 recreates ShieldDesc via ftCo_80092450 while
    #   x18 can remain live. If the shield overlap misses, BODY still consumes the GuardReflect
    #   motion-state submotion hurtcaps instead of an empty serialized snapshot.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_GuardReflect_Anim}
    # refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_GuardReflect
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    record = 11085
    defender = 0
    attacker = 1
    try:
        seed, out, ref = _step_one_row(dataset_path, record)
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            raise AssertionError(
                f"stale required validation dataset cache: rerun forced aggregate preprocess for {dataset_path}"
            ) from exc
        raise

    assert int(seed["action_id"][defender]) == 182  # GuardReflect
    assert int(seed["animation_index"][defender]) == 0xFFFFFFFF
    assert int(seed["action_frame"][defender]) == -2
    assert int(seed["guard_reflect_timer_x14"][defender]) == 0
    assert int(seed["guard_reflect_timer_x18"][defender]) > 0
    assert int(seed["seed_prev_action_id"][defender]) == 182
    assert int(seed["action_id"][attacker]) == 69  # AttackAirHi

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 87  # DamageFlyHi
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 7
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 45
    assert int(out["instance_hit_by"][defender]) == int(ref["instance_hit_by"][defender])
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-5)
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 7


@pytest.mark.integration
def test_guardreflect_active_x14_no_submotion_without_guardon_provenance_stays_no_body_gat_11085() -> None:
    # Boundary for the expired-x14 owner above: active x14 remains a ReflectDesc/raw-snapshot phase
    # unless the transition provenance is GuardOn. Mutating only x14 back to active must not turn
    # this no-submotion snapshot into generic GuardReflect BODY hurtcaps.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    record = 11085
    defender = 0
    attacker = 1
    try:
        ds = load_replay_buffers(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            raise AssertionError(
                f"stale required validation dataset cache: rerun forced aggregate preprocess for {dataset_path}"
            ) from exc
        raise
    seed = ds.rows["seed_t"][record : record + 1].copy()
    seed["guard_reflect_timer_x14"][0, defender] = np.uint8(1)
    out, _ref = _step_one_row_with_seed(dataset_path, record, seed)

    assert int(seed["action_id"][0, defender]) == 182
    assert int(seed["seed_prev_action_id"][0, defender]) == 182
    assert int(seed["guard_reflect_timer_x14"][0, defender]) == 1
    assert int(out["action_id"][defender]) == 182
    assert int(out["hitlag"][defender]) == 0
    assert int(out["hitstun"][defender]) == 0
    assert int(out["hitlag"][attacker]) == 0


@pytest.mark.integration
def test_guardreflect_active_x14_no_guardon_allows_later_seeded_shield_ewt_1218() -> None:
    # FoD rollout lock for the active-x14 no-submotion GuardReflect boundary:
    # - p1 is a carried GuardReflect raw snapshot with active x14 but no GuardOn provenance.
    # - The early p0 AttackAirB BODY candidates overlap stale serialized hurtcaps, but source
    #   ownership is still ReflectDesc until ftCo_80093BC0 expires x14.
    # - A later hitbox has replay-proven ShieldDesc contact and must still enter GuardSetOff with
    #   the lower 9-damage shield hitlag. This is the source-order complement to the GAT no-BODY
    #   negative above, not a blanket GuardReflect contact reject.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    #   ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092F2C}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "replays/validation/fountain_of_dreams_recent/"
        "ElatedWearyTermite.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    try:
        ds = load_replay_buffers(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            raise AssertionError(
                f"stale required validation dataset cache: rerun forced aggregate preprocess for {dataset_path}"
            ) from exc
        raise

    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    start = 1020
    target = 1218
    attacker = 0
    defender = 1
    seed = samples["seed_t"][target]
    ref = samples["ref_t1"][target]
    assert int(seed["action_id"][attacker]) == 67  # AttackAirB
    assert int(seed["action_id"][defender]) == 182  # GuardReflect
    assert int(seed["animation_index"][defender]) == 0xFFFFFFFF
    assert int(seed["action_frame"][defender]) < 0
    assert int(seed["guard_reflect_timer_x14"][defender]) > 0
    assert int(seed["guard_reflect_origin_guardon_u8"][defender]) == 0
    assert int(seed["seed_prev_action_id"][defender]) != 178  # not GuardOn
    assert int(ref["action_id"][defender]) == 181  # GuardSetOff
    assert int(ref["hitlag"][defender]) == 6

    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, target + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
        out = out_view[0].copy()
    finally:
        binding.destroy(handle)

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["animation_index"][defender]) == int(ref["animation_index"][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]), abs=1e-5)


@pytest.mark.integration
def test_guardreflect_expired_x14_rollout_orders_lower_body_before_later_shield_gat_11080() -> None:
    # Rollout-real lock for the same expired-x14 GuardReflect handoff:
    # - p1 AttackAirHi hitbox 0 misses ShieldDesc but overlaps p0's GuardOn hurtcap fallback.
    # - hitbox 1 would overlap the shield bubble in the simulator proxy, but decomp processes
    #   shield/BODY per HitCapsule in order, so the lower-index BODY hit commits first.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    start = 11080
    stop = 11086
    defender = 0
    attacker = 1
    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_view[0].copy()
            ref = samples["ref_t1"][record]
            for field in ("action_id", "animation_index", "hitlag", "hitstun", "shield_hp"):
                if out[field].dtype.kind in "iu":
                    assert int(out[field][defender]) == int(ref[field][defender]), (
                        f"record={record} field={field}"
                    )
                else:
                    assert float(out[field][defender]) == pytest.approx(
                        float(ref[field][defender]), abs=1e-5
                    ), f"record={record} field={field}"
        assert int(out_view[0]["action_id"][defender]) == 87  # DamageFlyHi
        assert int(out_view[0]["hitlag"][attacker]) == int(samples["ref_t1"][stop]["hitlag"][attacker])
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_guardreflect_expired_x14_later_shield_candidate_reports_earlier_body_gat_11085() -> None:
    # Diagnostic boundary for the rollout owner above: at the pre-combat snapshot, hitbox 1 is not
    # allowed to win as a shield hit because lower hitbox 0 has already reached BODY priority.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / (
        "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    start = 11080
    target = 11085
    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, target):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)

        prev_input_bytes = prev_input_u8[target : target + 1, :input_stride].copy()
        input_bytes = input_u8[target : target + 1, :input_stride].copy()
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        raw_cand, count_cand = binding.debug_shield_candidate_decisions(handle, 0, 128)
    finally:
        binding.destroy(handle)

    cand = raw_cand.reshape(-1).view(_DEBUG_SHIELD_CANDIDATE_DTYPE)[:count_cand]
    hb1 = [
        c
        for c in cand
        if int(c["attacker"]) == 1 and int(c["defender"]) == 0 and int(c["hitbox_id"]) == 1
    ]
    assert len(hb1) == 1
    assert int(hb1[0]["reject_reason"]) == 12
    assert int(hb1[0]["overlap_shield"]) == 0


@pytest.mark.integration
def test_guardreflect_x14_expiry_recreates_current_shielddesc_tch_5251() -> None:
    # Active-x14 GuardReflect callback boundary:
    # - p1 starts the frame on GuardReflect with x14==1 and x18 still live.
    # - `ftCo_GuardReflect_Anim -> ftCo_80093BC0` expires x14, recreates ShieldDesc via
    #   `ftCo_80092450`, then fighter-vs-fighter collision accepts p0 AttackAirHi into
    #   GuardSetOff without shield HP depletion because x18/x221C_b2 remains active.
    # - The direct one-step row locks the no-shield-damage side of that boundary; the rollout lock
    #   below clears the seed dependence by reaching the same contact from live simulation.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092450}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    record = 5251
    attacker = 0
    defender = 1
    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows["seed_t"][record : record + 1].copy()
    out, ref = _step_one_row_with_seed(dataset_path, record, seed)

    assert int(seed["action_id"][0, attacker]) == 69  # AttackAirHi
    assert int(seed["action_id"][0, defender]) == 182  # GuardReflect
    assert int(seed["animation_index"][0, defender]) == 0xFFFFFFFF
    assert int(seed["action_frame"][0, defender]) == -2
    assert int(seed["guard_reflect_timer_x14"][0, defender]) == 1
    assert int(seed["guard_reflect_timer_x18"][0, defender]) > 0

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 181  # GuardSetOff
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert float(out["shield_hp"][defender]) == pytest.approx(float(ref["shield_hp"][defender]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize("x14", [0, 2])
def test_guardreflect_current_shielddesc_requires_x14_expiry_boundary_tch_5251(x14: int) -> None:
    # Boundary negative for the TCH:5251 current-pose ShieldDesc owner:
    # - x14==2 is still in the pre-expiry ReflectDesc-only phase for no-submotion GuardReflect.
    # - x14==0 was already expired before this frame and lacks the x14 seed-to-zero callback.
    # Neither case may borrow the current-pose ShieldDesc handoff when the teacher-forced
    # ShieldDesc seed is absent.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    record = 5251
    defender = 1
    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows["seed_t"][record : record + 1].copy()
    seed["combat_shield_contact_hb_kind"][:] = np.uint8(0)
    seed["guard_reflect_timer_x14"][0, defender] = np.uint8(x14)
    out, _ref = _step_one_row_with_seed(dataset_path, record, seed)

    assert int(out["action_id"][defender]) == 182  # GuardReflect stays no-contact.
    assert int(out["hitlag"][defender]) == 0


@pytest.mark.integration
def test_guardreflect_already_expired_x14_seed_does_not_suppress_shield_damage_tch_5251() -> None:
    # Boundary negative for shield-damage suppression: replay-proven ShieldDesc contact can still
    # be teacher-forced when x14 was already expired, but this test isolates the old x14-only
    # boundary by clearing the independent x221C_b2 powershield owner. Without either source lane it
    # must not borrow x18 as a powershield no-damage owner.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    record = 5251
    defender = 1
    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows["seed_t"][record : record + 1].copy()
    seed["guard_reflect_timer_x14"][0, defender] = np.uint8(0)
    seed["state_flags"][0, defender, 3] = np.uint8(int(seed["state_flags"][0, defender, 3]) & ~0x20)
    out, ref = _step_one_row_with_seed(dataset_path, record, seed)

    assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 181
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert float(out["shield_hp"][defender]) < float(ref["shield_hp"][defender])


@pytest.mark.integration
def test_guardreflect_x14_expiry_rollout_reaches_jumpf_tch_5224() -> None:
    # Rollout-real lock for the disruptive TCH cluster:
    # - Starting at TCH:5224, p1 reaches GuardReflect at 5250.
    # - At 5251 the x14-expiry ShieldDesc handoff must accept p0 AttackAirHi into GuardSetOff.
    # - The later row 5283 then remains replay-aligned as JumpF instead of the previous false
    #   DamageAir3 divergence from the missed shield contact.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    start = 5224
    stop = 5284
    defender = 1
    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        seen_5251 = False
        for record in range(start, stop + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_view[0].copy()
            ref = samples["ref_t1"][record]
            if record == 5251:
                seen_5251 = True
                assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 181
                assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
                assert float(out["shield_hp"][defender]) == pytest.approx(
                    float(ref["shield_hp"][defender]), abs=1e-6
                )
            if record == 5283:
                assert int(out["action_id"][defender]) == int(ref["action_id"][defender]) == 25
                assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 0
        assert seen_5251
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_shieldbreakstand_furafura_entry_clears_colanim_hit_status_prh_11549_rollout() -> None:
    # Rollout-real lock for the disruptive former-F08b PRH cluster:
    # ShieldBreakStand keeps colanim hit status while standing up from shield break, but
    # ftCo_80099010 enters Furafura without KeepColAnimHitStatus / SkipColAnim. The destination
    # row must clear x198C-derived hurtbox_state rather than carrying invulnerability through the
    # dazed action.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_ShieldBreakStand.c::ftCo_80098F3C
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Furafura.c::ftCo_80099010
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/PositiveRevolvingHyena.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    start = 11549
    stop = 11565
    p = 0
    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_view[0].copy()
            ref = samples["ref_t1"][record]
            for field in ("action_id", "animation_index", "action_frame", "hurtbox_state"):
                assert int(out[field][p]) == int(ref[field][p]), f"record={record} field={field}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_landingairb_lcancel_rate_uses_hitlag_latched_lr_edge_fsp_6448_rollout() -> None:
    # Rollout-real lock for the disruptive former-F08b FSP cluster:
    # an LR press during AttackAirB hitlag is held in input.x668 while fp->x2219_b5 is active, so
    # x67F remains inside the L-cancel window when AttackAirB_Coll enters LandingAirB. Runtime must
    # carry that hitlag-latched input-history owner, otherwise LandingAirB runs at full landing lag
    # and action_frame falls behind replay.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_Spaghetti_8006AD10_Inner1,Fighter_Spaghetti_8006AD10}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/FavorableSuperficialPig.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    start = 6448
    stop = 6470
    p = 1
    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, stop + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out = out_view[0].copy()
            ref = samples["ref_t1"][record]
            for field in ("action_id", "animation_index", "action_frame", "hitlag", "l_cancel"):
                assert int(out[field][p]) == int(ref[field][p]), f"record={record} field={field}"
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_attacks3_angle_variant_hitbox_events_apply_with_variant_pose_agg_5611() -> None:
    # Angled side-tilt event ownership:
    # - AttackS3Hi/HiS/S/LwS/Lw use distinct submotions/poses but share the ftCo_AttackS3
    #   callbacks. The extracted command table stores the common hitbox events under
    #   ftCo_SM_AttackS3, so runtime aliases only the command-event lookup and still samples
    #   hitbox centers from the live angled submotion pose.
    # - AGG:5611 is Falco AttackS3Lw hitting Fox during SpecialAirHi. Without the command-event
    #   alias, no Falco hitboxes exist and Fox incorrectly continues SpecialAirHi.
    # refs/melee/src/melee/ft/ftmotionstates.c (AttackS3* table entries)
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS3.c
    # data/moves/{fox,falco}.json::moves["ftCo_SM_AttackS3"].events
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 5611)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 55  # ftCo_MS_AttackS3Lw
    assert int(seed["animation_index"][attacker]) == 57  # ftCo_SM_AttackS3Lw pose
    assert int(seed["action_frame"][attacker]) == 5
    assert int(ref["action_id"][defender]) == 88  # DamageFlyN
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6


@pytest.mark.integration
def test_attacks3_angle_variant_hitbox_events_do_not_pre_admit_agg_5610() -> None:
    # Negative neighbor for the AttackS3 angle event alias: the frame before AGG:5611 has the same
    # angled side-tilt owner but no accepted BODY hit. The alias must not become a broad
    # side-tilt-frame admission shortcut.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 5610)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 55  # ftCo_MS_AttackS3Lw
    assert int(seed["animation_index"][attacker]) == 57  # ftCo_SM_AttackS3Lw pose
    assert int(ref["action_id"][defender]) == 356  # SpecialAirHi
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_attacks3_angle_variant_hitbox_events_fix_rollout_agg_5577() -> None:
    # Rollout-real lock for the disruptive AGG cluster: starting from SpecialHiHoldAir at 5577,
    # the first visible split was Fox continuing SpecialAirHi through Falco's AttackS3Lw. The
    # source-shaped command-event alias should make the rollout hit match replay at 5611 without
    # needing any row-local bridge.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    start_record = 5577
    defender = 1
    try:
        seed_bytes[0, :] = seed_u8[start_record, :seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, 5612):
            prev_input_bytes[0, :] = prev_input_u8[record, :input_stride]
            input_bytes[0, :] = input_u8[record, :input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            out_row = out_view[0].copy()
            ref_row = samples["ref_t1"][record]
            if record < 5611:
                assert int(out_row["action_id"][defender]) == int(ref_row["action_id"][defender]), record
            else:
                assert int(out_row["action_id"][defender]) == int(ref_row["action_id"][defender]) == 88
                assert int(out_row["hitlag"][defender]) == int(ref_row["hitlag"][defender]) == 6
                assert int(out_row["hitstun"][defender]) == int(ref_row["hitstun"][defender]) == 47
                break
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_sustained_attackairn_edge_near_x7a8_still_enters_damage_qgd_8332() -> None:
    # Negative sentinel for the enable-edge phantom subset. QGD:8332 is a sustained AttackAirN
    # capsule near the x7A8 boundary; vanilla enters DamageAir2, so the tip-log subset must not
    # broaden into a generic overlap-margin suppression.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, 8332)
    defender = 0
    assert int(ref["action_id"][defender]) == 85  # DamageAir2
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_enable_edge_x58_x4c_no_translation_sweep_tch_5010() -> None:
    # Newly created HitCapsules use ftColl_8007AD18's HitCapsule_Enabled case: x4C is sampled
    # from the refreshed current pose and x58 is copied from x4C before BODY collision. A
    # teacher-forced reseed must not synthesize x58 by sweeping backward through this frame's
    # fighter translation on a per-hitbox enable edge.
    #
    # TCH:5010 is an enable-edge AttackAirLw row in the remaining exact lbColl narrowphase split;
    # the direct sweep check below protects the shared x58/x4C owner while that scalar residual is
    # worked separately.
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_8007AD18}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[5010:5011]
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        timing_raw = binding.debug_hitbox_event_timing(handle, 0, 1, 0)
        sweep_raw = binding.debug_hitbox_sweep_proxy(handle, 0, 1, 0)
    finally:
        binding.destroy(handle)

    timing_dtype = np.dtype(
        [
            ("attacker", "u1"),
            ("hb_id", "u1"),
            ("char_id", "u1"),
            ("_pad0", "u1"),
            ("msid", "<u2"),
            ("pose_frame", "<u2"),
            ("anim_frame_f32", "<f4"),
            ("frame_speed_mul_f32", "<f4"),
            ("start_frame", "<i2"),
            ("end_frame", "<i2"),
            ("enabled_prev", "u1"),
            ("enabled_cur", "u1"),
            ("prev_hit_group", "u1"),
            ("cur_hit_group", "u1"),
            ("pose_create_count", "u1"),
            ("pose_clear_count", "u1"),
            ("pose_clear_all_count", "u1"),
            ("enable_edge", "u1"),
            ("last_affect_kind_le", "u1"),
            ("last_affect_kind_eq", "u1"),
            ("last_affect_frame_le", "<u2"),
            ("last_affect_frame_eq", "<u2"),
            ("last_affect_u16_7_le", "<u2"),
            ("last_affect_u16_7_eq", "<u2"),
        ],
        align=False,
    )
    timing = timing_raw.reshape(-1).view(timing_dtype)[0]
    assert int(timing["msid"]) == 72  # AttackAirLw
    assert int(timing["enabled_prev"]) == 0
    assert int(timing["enabled_cur"]) == 1
    assert int(timing["enable_edge"]) == 1
    sweep_dtype = np.dtype(
        [
            ("attacker", "u1"),
            ("hb_id", "u1"),
            ("enabled_prev", "u1"),
            ("enabled_cur", "u1"),
            ("prev_valid", "u1"),
            ("cur_valid", "u1"),
            ("_pad0", "u1", (2,)),
            ("msid", "<u2"),
            ("pose_prev", "<u2"),
            ("pose_cur", "<u2"),
            ("char_id", "u1"),
            ("_pad1", "u1"),
            ("anim_frame_f32", "<f4"),
            ("prev_anim_frame_f32", "<f4"),
            ("frame_speed_mul_f32", "<f4"),
            ("prev_x", "<f4"),
            ("prev_y", "<f4"),
            ("prev_z", "<f4"),
            ("prev_radius", "<f4"),
            ("cur_x", "<f4"),
            ("cur_y", "<f4"),
            ("cur_z", "<f4"),
            ("cur_radius", "<f4"),
            ("u16_6_prev", "<u2"),
            ("u16_7_prev", "<u2"),
            ("u16_6_cur", "<u2"),
            ("u16_7_cur", "<u2"),
            ("arg3_var_r22_known", "u1"),
            ("arg3_var_r22_from_extracted", "u1"),
            ("arg3_var_r22_gates_collision", "u1"),
            ("_pad2", "u1"),
        ],
        align=False,
    )
    sweep = sweep_raw.reshape(-1).view(sweep_dtype)[0]
    assert int(sweep["enabled_prev"]) == 0
    assert int(sweep["enabled_cur"]) == 1
    assert int(sweep["prev_valid"]) == 0
    assert int(sweep["cur_valid"]) == 1


@pytest.mark.integration
def test_grounded_overlap_z_depth_rejects_false_attackdash_hhg_6740() -> None:
    # Grounded fighter-overlap depth lane:
    # - ftCommon_8007DD7C writes xF8_playerNudgeVel.y from p_ftCommonData->x454.
    # - ftCommon_8007E0E4 clamps that hidden engine-space Z lane with x458.
    # - Fighter_procUpdate applies the lane before collision primitives are refreshed, so
    #   ftColl_80078C70/lbColl_80006E58 see separated grounded BODY primitives.
    # HHG:6740 was a false AttackDash->KneeBend BODY hit while replay-visible Slippi pos_z was 0.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/HilariousVillainousGiraffe.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, 6740)
    defender = 1
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_landingairn_float_aobj_hurtcaps_reject_false_attackdash_tch_1816() -> None:
    # LandingAirN can carry a non-integer AObj frame because its animation rate is set from
    # landing lag. Hurtcap endpoints must sample the live HSD AObj/FObj local-SRT pose before
    # lb_8000B1CC; integer SSANIM floor admitted a false AttackDash BODY hit here.
    #
    # This is not a replay admission bridge: the runtime path evaluates extracted SSANIMT1 FObj
    # tracks and then uses the normal BODY selector.
    # refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    # refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[1816:1817]
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        selected_raw, selected_count = binding.debug_combat_select_body_hits(handle, 0, 64)
    finally:
        binding.destroy(handle)

    assert int(row["seed_t"]["action_id"][0, 1]) == 70  # LandingAirN
    assert float(row["seed_t"]["anim_frame_f32"][0, 1]) != float(int(row["seed_t"]["anim_frame_f32"][0, 1]))
    assert int(selected_count) == 0
    assert selected_raw.shape[0] >= 1

    _seed, out, ref = _step_one_row(dataset_path, 1816)
    defender = 1
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_fox_attackdash_static_collision_pose_rejects_false_attackairhi_fsp_7078() -> None:
    # AttackAirHi vs AttackDash allow-interrupt tail boundary:
    # - FSP:7078 has the selected late AttackAirHi hb2 source overlapping only the defender's
    #   part-18 tail cap while grounded AttackDash has crossed its generated allow_interrupt event.
    # - Clear the seeded HitCapsule victim rings before stepping so this lock exercises the BODY
    #   geometry owner directly; otherwise one-step replay seed state can mask the false contact.
    # - The adjacent FSP:5765 positive keeps pre-allow-interrupt AttackDash BODY admission intact,
    #   and FSP:7080 remains the later Wait-frame UpAir hit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
    #   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirHi/events.ftCo_SM_AttackDash
    # data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/FavorableSuperficialPig.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows[7078:7079]["seed_t"].copy()
    attacker = 0
    defender = 1
    assert int(seed["action_id"][0, attacker]) == 68  # AttackAirHi
    assert int(seed["action_id"][0, defender]) == 50  # AttackDash
    assert int(ds.rows[7080]["ref_t1"]["action_id"][defender]) == 90  # DamageFlyTop.
    seed["combat_hitlist_cd"][0, attacker, :, defender] = np.uint16(0)
    seed["combat_hitlist_victim_iid"][0, attacker, :, defender] = np.uint16(0)
    seed["combat_hitlist_hb_valid"][0, attacker, :] = np.uint8(0)
    seed["combat_hitlist_hb_cd"][0, attacker, :, defender] = np.uint16(0)
    seed["combat_hitlist_hb_victim_iid"][0, attacker, :, defender] = np.uint16(0)

    out, ref = _step_one_row_with_seed(dataset_path, 7078, seed)
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_attackairlw_attackdash_allow_interrupt_tail_rejects_high_capsule_fsp_1270() -> None:
    # AttackAirLw vs AttackDash allow-interrupt tail boundary:
    # - FSP:1270 has Fox AttackAirLw hb0/hb1 active against a grounded Fox AttackDash defender
    #   exactly at the generated AttackDash allow_interrupt phase.
    # - Source rejects the part-18 tail/hb0 BODY owner and keeps the lower same-group hb1/body
    #   contact eligible, producing the 2-damage hitlag/percent lane.
    # - This is bounded by extracted data, not a row branch: data/moves/{fox,falco}.json provides
    #   the AttackAirLw same-group 3/2-damage payload and AttackDash allow_interrupt event, while
    #   data/hurtcaps/{fox,falco}.json identifies cap12 as part 18. The nearby FSP:5765 positive
    #   and FSP:7078 negative keep the broader AttackDash collision-pose policy unchanged.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
    #   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/FavorableSuperficialPig.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 1270)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw
    assert int(seed["action_id"][defender]) == 50  # AttackDash
    assert int(ref["action_id"][defender]) == 79  # DamageN2
    assert float(ref["percent"][defender]) == 25.0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent", "instance_hit_by", "last_hit_by"):
        np.testing.assert_array_equal(out[field], ref[field], err_msg=f"field={field}")


@pytest.mark.integration
def test_fox_attackdash_static_collision_pose_rejects_false_attackairhi_fsp_7078_rollout() -> None:
    # Rollout-mode negative lock for the former AttackDash frame-34 over-admit:
    # FSP:7078/7079 has the same visible AttackDash family as the positive frame-34 slice, but it
    # is after allow_interrupt and must reject the part-18 tail-only AttackAirHi hb2 BODY contact
    # through the frame-0 AttackDash -> Wait callback boundary. FSP:7080 is the later ordinary
    # Wait-frame UpAir hit and remains positive.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
    #   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
    # Source of timing windows: data/scripts/{fox,falco}.bin (MSLFTSC1).
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/FavorableSuperficialPig.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    start_record = 5391
    target_records = {7079, 7080}
    target_record = max(target_records)
    seed = ds.rows[start_record : start_record + 1]["seed_t"].copy()
    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        rows: dict[int, tuple[np.void, np.void]] = {}
        for record in range(start_record, target_record + 1):
            row = ds.rows[record : record + 1]
            prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            frame_seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, seed_stride
            )
            binding.step_input_replay_frame_rng(handle, frame_seed_bytes, prev_input_bytes, input_bytes)
            if record in target_records:
                binding.write_compare(handle, out_compare_bytes)
                rows[record] = (
                    out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(),
                    row["ref_t1"][0].copy(),
                )
    finally:
        binding.destroy(handle)

    defender = 1
    assert set(rows) == target_records
    out, ref = rows[7079]
    assert int(ref["action_id"][defender]) == 14  # Wait, not same-frame DamageFlyTop.
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"record=7079 field={field}"
    out, ref = rows[7080]
    assert int(ref["action_id"][defender]) == 90  # Later ordinary Wait-frame DamageFlyTop hit.
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"record=7080 field={field}"


@pytest.mark.integration
def test_attackdash_post_hitbox_pre_iasa_pose_selects_downsmash_body_fsp_5765() -> None:
    # AttackDash late collision-pose phase:
    # - Fox AttackDash has already run its script hitbox clear, but has not crossed its
    #   command-script allow_interrupt frame.
    # - BODY collision consumes the post-clear JObj collision pose for the defender capsules; the
    #   ordinary replay-visible frame pose misses this DownSmash low hit.
    # - The adjacent FSP:7078 negative above is after the AttackDash allow_interrupt frame and
    #   stays exact, proving this is not a broad AttackDash pose offset.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
    #   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/FavorableSuperficialPig.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 5765)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 67  # AttackLw4
    assert int(seed["action_id"][defender]) == 50  # AttackDash
    assert int(ref["action_id"][defender]) == 80  # DamageHi1
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent"):
        np.testing.assert_array_equal(out[field], ref[field], err_msg=f"field={field}")


@pytest.mark.integration
def test_fox_jumpb_dynamic_chain_selects_attackairb_body_ppa_3182() -> None:
    # Fox JumpB dynamic-chain collision pose:
    # - Dolphin pre-ftColl primitive probes on PPA:3182 show Falco AttackAirB's hitbox already
    #   matches runtime, while Fox hurtcap-12 endpoints consume the live ftData.x2C dynamic JObj
    #   chain before lb_8000B1CC/lbColl_80006E58.
    # - SSDYNN01 v4's data-owned collision-msid predicate includes JumpB; this is not a broad
    #   JumpB facing, distance, or row-local BODY admission shortcut.
    # refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/PriceyPartialAlbatross.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 3182)
    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 26  # JumpB
    assert int(seed["animation_index"][defender]) == 17
    assert int(seed["action_id"][attacker]) == 67  # AttackAirB
    assert int(ref["action_id"][defender]) == 85  # DamageAir3
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 7


@pytest.mark.integration
def test_sheik_attackairb_same_group_selects_hb2_before_outer_hb3_tsh_3935() -> None:
    # Sheik BAir same-group BODY selection:
    # - TSH:3935 creates four BAir HitCapsules on frame 4. hb2 and hb3 both overlap Falco JumpF
    #   hurtcaps, but source ftColl records the first accepted same-group BODY DmgLog entry and
    #   applies the 10-damage hb2 result before the larger outer hb3 can widen the hit.
    # - The Fox/Falco AttackAirB model-scale cancellation lane is payload-owned by their 15/9
    #   damage extracted BAir split; Sheik's 10/14 damage hb2/hb3 payload must remain on the
    #   ordinary lbColl matrix path rather than borrowing that scale-only counterfactual.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # data/moves/{fox,falco,sheik}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/sheik/TenseSameHummingbird.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    record = 3935
    row = ds.rows[record : record + 1]
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        selected_raw, selected_count = binding.debug_combat_select_body_hits(handle, 0, 64)
    finally:
        binding.destroy(handle)

    attacker = 0
    defender = 1
    assert int(row["seed_t"]["action_id"][0, attacker]) == 67  # AttackAirB.
    assert int(row["ref_t1"]["hitlag"][0, attacker]) == 6
    assert int(selected_count) == 1
    assert int(selected_raw[0, 0]) == attacker
    assert int(selected_raw[0, 1]) == defender
    assert int(selected_raw[0, 2]) == 2  # hb2, not the higher-damage outer hb3.

    _seed, out, ref = _step_one_row(dataset_path, record)
    assert int(ref["action_id"][defender]) == 85  # DamageAir2 from hb2.
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 6
    assert int(out["hitlag"][defender]) == int(ref["hitlag"][defender]) == 6
    assert int(out["hitstun"][defender]) == int(ref["hitstun"][defender]) == 12
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "defender"),
    [
        ("FavorableSuperficialPig.slpz", 1499, 1),
        ("ImpassionedAlarmedTarsier.slpz", 2161, 0),
        ("ImpassionedAlarmedTarsier.slpz", 6196, 1),
        ("TubbyCurlyHerring.slpz", 5740, 1),
    ],
)
def test_fox_jumpb_dynamic_chain_does_not_broaden_body_admission_controls(
    dataset_name: str, record: int, defender: int
) -> None:
    # Negative controls from the rejected broad JumpB-facing experiment. These rows were exact
    # before the retained SSDYNN01 data predicate and must stay exact, proving the owner is not a
    # generic JumpF/B pose flip or BODY tolerance.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, record)
    for field in ("action_id", "animation_index", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_fox_escapeair_dynamic_tail_chain_selects_marth_fair_tip_wws_2580() -> None:
    # Fox EscapeAir dynamic-chain BODY pose:
    # - Dolphin lbColl_8000805C probes on WWS:2580 show Marth Fair hb0/hb1/hb2 all miss, then
    #   hb3 enters ftColl_80076ED8 against Fox's live dynamic tail-chain hurtcap.
    # - The source owner is the generated SSDYNN01 collision-msid predicate for Fox
    #   ftCo_SM_EscapeAir (msid 44), not a Marth Fair or row-local hitbox preference.
    # - Adjacent frames 2578/2579 remain no-hit, and the hitlag tail stays exact after the hb3
    #   selection, proving this does not broaden EscapeAir BODY admission.
    # refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # data/anims/fox.dyn.bin::SSDYNN01 collision_motion_state_ids(ftCo_SM_EscapeAir)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/marth/WellWornSmallGoshawk.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    for record in (2578, 2579):
        seed, out, ref = _step_one_row(dataset_path, record)
        defender = 0
        attacker = 1
        assert int(seed["action_id"][defender]) == 236  # EscapeAir.
        assert int(seed["animation_index"][defender]) == 44
        assert int(seed["action_id"][attacker]) == 66  # AttackAirF.
        assert int(ref["action_id"][defender]) == 236
        for field in ("action_id", "animation_index", "hitlag", "hitstun"):
            assert int(out[field][defender]) == int(ref[field][defender]), f"record={record} field={field}"
        assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)

    seed, contacts = _collect_contact_debug_for_row(dataset_path, 2580)
    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 236
    assert int(seed["animation_index"][defender]) == 44
    assert int(seed["action_id"][attacker]) == 66
    accepted = [
        c
        for c in contacts
        if int(c["attacker"]) == attacker and int(c["defender"]) == defender and int(c["contact_kind"]) == 0
    ]
    assert accepted
    assert {int(c["hitbox_id"]) for c in accepted} == {3}
    assert all(float(c["hitbox_damage"]) == pytest.approx(13.0) for c in accepted)

    for record in (2580, 2581, 2582):
        _seed, out, ref = _step_one_row(dataset_path, record)
        for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
            assert int(out[field][defender]) == int(ref[field][defender]), f"record={record} field={field}"
        assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "expected_action", "expected_hitlag"),
    [
        (4921, 212, 0),
        (4922, 212, 0),
        (4923, 79, 6),
    ],
)
def test_fox_catch_dynamic_tail_chain_keeps_dair_body_timing_mgs(
    record: int, expected_action: int, expected_hitlag: int
) -> None:
    # Fox Catch dynamic-chain collision pose:
    # - Falco DAir's active hitbox overlaps Fox's part-18 tail cap near Catch frames 8..10.
    # - Vanilla ftColl consumes Fox's ftData.x2C dynamic JObj chain through ftCo_8009E0A8 before
    #   lbColl BODY tests; the descriptor cone clamp from lb_8001044C keeps frames 8 and 9 out,
    #   while frame 10 reaches DamageN2.
    # - The SSDYNN01 collision-owner predicate names ftCo_SM_Catch, so this is not a local
    #   AttackAirLw, FoD, or record-window BODY suppression.
    # refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    # refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E0A8}
    # refs/melee/src/melee/lb/lb_00F9.c::lb_8001044C
    # data/anims/fox.dyn.bin::SSDYNN01 collision_motion_state_ids(ftCo_SM_Catch)
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/fountain_of_dreams_recent/MilkyGracefulStingray.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    try:
        seed, out, ref = _step_one_row(dataset_path, record)
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            raise AssertionError(
                f"stale required validation dataset cache: rerun forced aggregate preprocess for {dataset_path}"
            ) from exc
        raise
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 69  # AttackAirLw
    assert int(seed["action_id"][defender]) == 212  # Catch
    assert int(seed["animation_index"][defender]) == 242
    assert int(ref["action_id"][defender]) == expected_action
    assert int(ref["hitlag"][defender]) == expected_hitlag
    for field in (
        "action_id",
        "animation_index",
        "hitlag",
        "hitstun",
        "percent",
        "instance_hit_by",
        "last_hit_by",
    ):
        np.testing.assert_array_equal(out[field], ref[field], err_msg=f"field={field}")


@pytest.mark.integration
def test_late_attackairhi_hitcapsule_latch_rejects_false_wait_hit_fsp_7079() -> None:
    # Late AttackAirHi victim-list owner:
    # - Fox/Falco UpAir clears the early hitboxes and recreates same-group late hitboxes.
    # - After that recreate edge, lbColl_8000ACFC owns repeat suppression by HitCapsule victim
    #   pointer. Slippi BODY attribution can still name an older source and the victim instance_id
    #   can advance on a same-frame Wait entry, so the dense seed latch must survive the stale
    #   attribution trim.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/FavorableSuperficialPig.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 7079)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][attacker]) == 68  # AttackAirHi
    assert int(seed["action_id"][defender]) == 50  # AttackDash
    assert int(ref["action_id"][defender]) == 14  # Wait, not DamageFlyTop
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_jumpf_tap_crossing_iasa_preempts_false_attacklw4_body_fsp_4852() -> None:
    # JumpF/B live-input IASA tap crossing:
    # - ftCo_Jump_IASA reaches ftCo_800CB870 before BODY collision.
    # - FSP:4852 seeds Fox in JumpF frame 0 from a pre-input KneeBend Anim entry. The later
    #   Fighter_Spaghetti input-history pass overwrites ftCo_Jump_Enter's transient x671=0xFE to a
    #   low timer because the stick freshly crossed the tilt threshold, and the current frame then
    #   crosses tap-jump threshold. Source enters JumpAerialF before Fox AttackLw4 BODY selection,
    #   so the false hit must not be admitted.
    # refs/melee/src/melee/ft/fighter.c::{
    #   Fighter_Spaghetti_8006AD10,Fighter_procUpdate}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_800CB870
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/FavorableSuperficialPig.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 4852)
    attacker = 0
    defender = 1
    assert int(seed["action_id"][defender]) == 25  # JumpF
    assert int(seed["action_frame"][defender]) == 0
    assert int(seed["tilt_timer_y"][defender]) < 4
    assert int(ref["action_id"][defender]) == 27  # JumpAerialF
    for field in ("action_id", "animation_index", "jumps_left", "hitlag", "hitstun"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert int(out["hitlag"][attacker]) == int(ref["hitlag"][attacker]) == 0


@pytest.mark.integration
def test_jumpf_tap_crossing_iasa_requires_fresh_tap_threshold_crossing_fsp_2123() -> None:
    # Negative boundary for the JumpF/B tap-crossing reconstruction: held-up snapshots whose
    # previous stick is already above the tap threshold must continue to rely on x671/XY edges,
    # not become a broad "JumpF plus high Y means double jump" shortcut.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/FavorableSuperficialPig.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 2123)
    defender = 0
    assert int(seed["action_id"][defender]) == 25  # JumpF
    assert int(seed["action_frame"][defender]) == 0
    assert int(seed["tilt_timer_y"][defender]) == 0xFE
    assert int(ref["action_id"][defender]) == 25
    assert int(out["action_id"][defender]) == int(ref["action_id"][defender])


@pytest.mark.integration
def test_lbcoll_matrix_first_admits_grounded_attackhi3_row_ppa_2614() -> None:
    # Matrix-first lbColl BODY predicate:
    # - lbColl_8000805C forwards to lbColl_80006E58 for BODY admission. The matrix-derived scalar
    #   can accept a contact that a simple world sphere/capsule prefilter rejects.
    # - PPA:2614 protects that the runtime runs the matrix predicate directly when pose data is
    #   available instead of using the simple overlap as a prefilter.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/PriceyPartialAlbatross.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 2614)
    defender = 1
    assert int(seed["action_id"][0]) == 67  # AttackAirB
    assert int(seed["action_id"][defender]) == 56  # AttackHi3
    assert int(ref["hitlag"][defender]) > 0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_grounded_hidden_depth_carry_rejects_false_attackdash_tch_5649() -> None:
    # Grounded overlap hidden-depth carry:
    # - ftCommon_8007DD7C / ftCommon_8007E0E4 accumulate the hidden engine-space Z lane before
    #   collision primitives are refreshed.
    # - Slippi seeds visible `pos_z` as zero here, but a prefix-causal reconstruction from grounded
    #   pushbox overlap yields p0=-0.4/p1=+0.4, matching the vanilla collision-probe separation and
    #   rejecting the false AttackDash -> Wait BODY hit.
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent/TubbyCurlyHerring.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, 5649)
    attacker = 1
    defender = 0
    assert int(seed["action_id"][attacker]) == 50  # AttackDash
    assert int(seed["action_id"][defender]) == 14  # Wait
    assert float(seed["pos_z"][defender]) == pytest.approx(-0.4, abs=1e-6)
    assert float(seed["pos_z"][attacker]) == pytest.approx(0.4, abs=1e-6)
    assert int(ref["action_id"][defender]) == 14  # Wait, not DamageFlyTop
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_attackairn_neutral_hitcapsule_latch_suppresses_false_wait_hit_his_2752() -> None:
    # AttackAirN dense-seed victim latch:
    # - lbColl_8000ACFC suppresses by HitCapsule.victims_1 victim presence, not by BODY
    #   `instance_hit_by` attribution.
    # - HIS:2752 has a neutral defender whose dense seed victim iid still matches the live fighter;
    #   preserving that latch rejects the false AttackAirN->Wait BODY hit while later owner slices
    #   continue to clear stale latches on proven refresh/admission rows.
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
    # refs/melee/src/melee/lb/types.h::HitCapsule
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/HungryImportantSnake.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    _seed, out, ref = _step_one_row(dataset_path, 2752)
    defender = 0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_attackairn_wait_rollout_bridge_not_used_by_ordinary_one_step_his_2752() -> None:
    # Package-boundary negative for the retained HIS rollout bridge:
    # - ordinary one-step reseed is allowed to use the explicit dense HitCapsule seed when present,
    #   but it must not consume the x18c8/last_hit_by fallback that exists only to reconstruct long
    #   replay-rollout hidden victim provenance.
    # - Clearing the dense row-local seed leaves only that fallback evidence; ordinary reseed must
    #   admit the live BODY hit rather than suppressing it as if replay_rollout_reseeded were set.
    # refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/HungryImportantSnake.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    try:
        ds = load_replay_buffers(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            raise AssertionError(
                f"stale required validation dataset cache: rerun forced aggregate preprocess for {dataset_path}"
            ) from exc
        raise
    record = 2752
    attacker = 1
    defender = 0
    seed = ds.rows["seed_t"][record : record + 1].copy()
    assert int(seed["combat_hitlist_cd"][0, attacker, 0, defender]) != 0
    seed["combat_hitlist_cd"][0, attacker, 0, defender] = np.uint16(0)
    seed["combat_hitlist_victim_iid"][0, attacker, 0, defender] = np.uint16(0)

    out, ref = _step_one_row_with_seed_one_step(dataset_path, record, seed)

    assert int(ref["action_id"][defender]) == 14  # Wait: dense seed suppresses the real replay row.
    assert int(out["action_id"][defender]) != int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) > 0


@pytest.mark.integration
@pytest.mark.parametrize(("record", "defender"), [(2753, 0), (3126, 0)])
def test_attackairn_stale_latch_wait_post_entry_and_jumpf_admit_real_hits_his(
    record: int, defender: int
) -> None:
    # Adjacent positive locks for the AttackAirN dense-seed victim latch:
    # - HIS:2752 preserves the Wait entry-frame victims_1 latch.
    # - HIS:2753 and HIS:3126 must still clear stale dense fallback entries so the live late
    #   AttackAirN HitCapsule can enter ftColl_80076ED8.
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root / "replays/validation/aggregate_recent/HungryImportantSnake.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, record)
    attacker = 1
    assert int(seed["action_id"][attacker]) == 65  # AttackAirN
    assert int(ref["hitlag"][defender]) > 0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
@pytest.mark.parametrize("record", [5136, 5137])
def test_forensic_optional_attackairb_source_clear_landing_latch_suppresses_false_selfplay_body(
    record: int,
) -> None:
    # Optional replay-forensic self-play check for the AttackAirB source-clear landing latch.
    # Package coverage for this owner is the committed synthetic guard below; this local triage
    # dataset check is intentionally not required for package review.
    #
    # AttackAirB source-clear landing latch:
    # - Fox BAir has already hit the victim and the dense same-hit_group HitCapsule seed still
    #   names the current victim object through the DamageN -> Landing handoff.
    # - Vanilla keeps Landing; without reconstructing the HitCapsule victims_1 owner the simulator
    #   admits a one-frame-early BAir BODY rehit into DamageHi3.
    # - This is hidden HitCapsule provenance, not a BODY geometry tolerance; clearing the dense
    #   seed in the adjacent negative below must admit the contact.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A360}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "reports/triage/mainline_selfplay_replays/mainline_selfplay_20260514T083640/"
        / "reports/triage/mainline_selfplay_replays/Game_20260514T083640.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing optional local self-play triage dataset: {dataset_path}")

    try:
        seed, out, ref = _step_one_row(dataset_path, record)
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            pytest.skip(f"stale optional local self-play triage dataset: {dataset_path}")
        raise
    attacker = 1
    defender = 0
    assert int(seed["action_id"][attacker]) == 67  # AttackAirB
    assert int(seed["action_id"][defender]) == 42  # Landing
    assert int(seed["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][attacker, 0, defender]) == int(
        seed["instance_id"][defender]
    )
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "percent"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "defender"),
    [
        ("DraftyHealthyHare.slpz", 482, 1),
        ("DraftyHealthyHare.slpz", 9768, 1),
        ("DraftyHealthyHare.slpz", 9861, 1),
        ("LoudDullGoat.slpz", 5270, 0),
    ],
)
def test_marth_aerial_static_spacie_tail_cap12_stays_suppressed(
    dataset_name: str, record: int, defender: int
) -> None:
    # Static Fox/Falco cap12 is the dynamic tail-chain slot (FtPart 18). Marth aerials must not
    # treat a regenerated static SSANIM overlap with that slot as full BODY damage unless SSDYNN01
    # marks the defender submotion as a live collision owner. Existing EscapeAir/Catch tests above
    # keep the dynamic positives intact.
    # refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    # data/anims/fox.dyn.bin::SSDYNN01 collision_motion_state_ids
    # data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/marth" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, record)
    attacker = 1 - defender
    assert int(seed["char_id"][attacker]) == 18  # Marth.
    assert int(seed["char_id"][defender]) in (1, 22)  # Fox/Falco.
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)


@pytest.mark.integration
@pytest.mark.parametrize("record", [1747, 4086])
def test_marth_attackairn_no_submotion_lightshield_guard_static_pose_stays_quiet_bme(
    record: int,
) -> None:
    # Falco no-submotion Guard/lightshield rows expose ShieldDesc ownership but no live source
    # hurtcap packet. Rebuilding fallback BODY caps from the static Guard submotion over-admits
    # Marth NAir hb0 pokes; source keeps the row in Guard with no BODY damage. Falco/Fox aerial
    # shield-poke positives stay outside this Marth-specific boundary.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_Guard_Anim,ftCo_80091E78}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # data/moves/marth.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
    # data/hurtcaps/falco.json static Guard fallback capsules
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/marth/BreakableMundaneElephant.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, record)
    defender = 0
    assert int(seed["char_id"][defender]) == 22  # Falco.
    assert int(seed["action_id"][defender]) == 179  # Guard.
    assert int(seed["animation_index"][defender]) == 0xFFFFFFFF
    assert float(seed["lightshield_amount"][defender]) > 0.0
    assert int(seed["char_id"][1 - defender]) == 18  # Marth.
    assert int(seed["action_id"][1 - defender]) == 65  # AttackAirN.
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by", "last_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)


@pytest.mark.integration
def test_forensic_optional_attackairb_source_clear_landing_latch_requires_dense_hitcapsule_seed_selfplay() -> None:
    # Optional replay-forensic negative for the BAir Landing source-clear owner. Package coverage
    # for this dense-HitCapsule boundary is the committed synthetic guard below.
    #
    # Source attribution alone is not enough to suppress a BODY contact. The dense HitCapsule
    # victim seed is the hidden owner proof.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "reports/triage/mainline_selfplay_replays/mainline_selfplay_20260514T083640/"
        / "reports/triage/mainline_selfplay_replays/Game_20260514T083640.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing optional local self-play triage dataset: {dataset_path}")

    try:
        ds = load_replay_buffers(str(dataset_path))
    except ValueError as exc:
        if "record_size mismatch" in str(exc):
            pytest.skip(f"stale optional local self-play triage dataset: {dataset_path}")
        raise
    record = 5136
    attacker = 1
    defender = 0
    seed = ds.rows["seed_t"][record : record + 1].copy()
    seed["combat_hitlist_cd"][0, attacker, 0, defender] = np.uint16(0)
    seed["combat_hitlist_victim_iid"][0, attacker, 0, defender] = np.uint16(0)

    out, ref = _step_one_row_with_seed_one_step(dataset_path, record, seed)

    assert int(ref["action_id"][defender]) == 42  # Landing: dense seed suppresses the real row.
    assert int(out["action_id"][defender]) != int(ref["action_id"][defender])
    assert int(out["hitlag"][defender]) > 0


def test_attackairb_source_clear_landing_dense_hitcapsule_suppression_synthetic() -> None:
    # Committed package guard for the AttackAirB source-clear Landing owner:
    # same-source Landing + x18C8 + same attacker instance is not enough by itself. The dense
    # HitCapsule victim seed (`combat_hitlist_cd == 0xFFFF`) is the hidden source owner that
    # suppresses the full BODY rehit. Clearing only that dense proof admits the contact.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A360}
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])
    hit_grounded = 1 << 9
    hit_aerial = 1 << 10

    seed_bytes = np.zeros((1, seed_stride), dtype=np.uint8)
    seed = seed_bytes.view(SEED_DTYPE).reshape(-1)
    attacker = 0
    defender = 1
    seed["stage_id"][0] = np.uint32(32)
    seed["num_players"][0] = np.uint8(2)
    seed["char_id"][0, :2] = np.uint8(1)
    seed["stocks"][0, :2] = np.uint8(4)
    seed["instance_id"][0, attacker] = np.uint16(111)
    seed["instance_id"][0, defender] = np.uint16(222)
    seed["action_id"][0, attacker] = np.uint16(67)  # AttackAirB
    seed["action_frame"][0, attacker] = np.int16(20)
    seed["source_port0"][0, attacker] = np.uint8(attacker)
    seed["action_id"][0, defender] = np.uint16(42)  # Landing
    seed["action_frame"][0, defender] = np.int16(3)
    seed["on_ground"][0, defender] = np.uint8(1)
    seed["source_clear_timer_x18c8"][0, defender] = np.uint8(3)
    seed["last_hit_by"][0, defender] = np.uint8(attacker)
    seed["instance_hit_by"][0, defender] = np.uint16(111)

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(batch_size=1, num_players=2, ucf_enabled=1, ucf_cardinals_1_0_enabled=1)
    try:
        for dense_seed, expected_hitlag, expected_percent in ((1, 0, 0.0), (0, 4, 5.0)):
            seed["combat_hitlist_cd"][0, attacker, 0, defender] = np.uint16(
                0xFFFF if dense_seed else 0
            )
            seed["combat_hitlist_victim_iid"][0, attacker, 0, defender] = np.uint16(
                222 if dense_seed else 0
            )
            binding.reseed_seed(handle, seed_bytes)
            binding.debug_set_hitbox_world(handle, 0, attacker, 0, 0.0, 0.0, 0.0, 2.0, 5.0, 1)
            binding.debug_set_hitbox_flags(handle, 0, attacker, 0, hit_grounded | hit_aerial)
            binding.debug_set_hitbox_group(handle, 0, attacker, 0, 0)
            binding.debug_set_hurtcap_world(
                handle, 0, defender, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 1.0
            )
            binding.debug_combat_resolve(handle)
            binding.write_compare(handle, out_bytes)
            out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
            assert int(out["action_id"][defender]) == 42
            assert int(out["hitlag"][defender]) == expected_hitlag
            assert float(out["percent"][defender]) == pytest.approx(expected_percent)
    finally:
        binding.destroy(handle)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "defender", "expected_seed_action", "expected_ref_action"),
    [
        ("DistinctCaringCobra.slpz", 8844, 0, 15, 14),  # WalkSlow -> Wait
        ("ImpassionedAlarmedTarsier.slpz", 6287, 1, 20, 21),  # Dash -> Run
        ("TubbyCurlyHerring.slpz", 5842, 1, 41, 15),  # SquatRv -> WalkSlow
    ],
)
def test_same_frame_locomotion_entry_hurtcaps_use_previous_jobj_pose_for_body_collision(
    dataset_name: str, record: int, defender: int, expected_seed_action: int, expected_ref_action: int
) -> None:
    # Same-frame common locomotion entry pose order:
    # - Fighter_8006A360 has already interpreted the previous action's JObj pose.
    # - Input/IASA can enter Wait/Run/Walk before collision, but these paths do not perform an
    #   immediate ftAnim_8006EBA4 tick for the new pose before lb_8000B1CC consumers run.
    # - BODY hurtcaps therefore use the previous live JObj pose for this collision pass while the
    #   replay-visible action/timebase has already moved to the new state.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, record)
    assert int(seed["action_id"][defender]) == expected_seed_action
    assert int(ref["action_id"][defender]) == expected_ref_action
    assert int(ref["hitlag"][defender]) == 0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"


@pytest.mark.integration
def test_turn_to_walkslow_entry_hurtcaps_use_turn_pose_for_shine_body_iat_5052_rollout() -> None:
    # Turn -> WalkSlow entry boundary:
    # - ftCo_Turn_Anim has already interpreted the live Turn JObj pose for the frame.
    # - ftCo_Turn_IASA can then enter WalkSlow before BODY collision, but the collision pass still
    #   consumes the serialized Turn pose rather than a projected next Turn frame or WalkSlow frame.
    # - IAT:5052 rolls to IAT:5092, where projecting Turn+1 admits a false Shine BODY hit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_Anim,ftCo_Turn_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Enter
    # refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "replays/validation/aggregate_recent/"
        "ImpassionedAlarmedTarsier.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_u8, prev_input_u8, input_u8 = _dataset_byte_views(ds)

    start = 5052
    target = 5092
    attacker = 0
    defender = 1
    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(1)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, target + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_view[0].copy()
    ref = samples["ref_t1"][target]
    assert int(ref["action_id"][attacker]) == 360  # Fox SpecialLwStart
    assert int(ref["action_id"][defender]) == 15  # WalkSlow
    assert int(ref["hitlag"][attacker]) == 0
    assert int(ref["hitlag"][defender]) == 0
    assert int(ref["hitstun"][defender]) == 0
    for field in ("action_id", "animation_index", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][attacker]) == int(ref[field][attacker]), f"attacker field={field}"
        assert int(out[field][defender]) == int(ref[field][defender]), f"defender field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)
    assert float(out["pos_x"][defender]) == pytest.approx(float(ref["pos_x"][defender]), abs=3e-5)


@pytest.mark.integration
def test_marth_dash_to_guardon_uses_frame_start_dash_body_pose_vsa_4317() -> None:
    # Dash -> GuardOn Ft_MF_SkipAnim BODY pose owner:
    # - Probe-backed vanilla evidence on VSA:4317 shows ftCo_800924C0 publishes GuardOn/-1 after
    #   Dash_Anim has interpreted the live JObj tree, then ftColl_80078C70/lbColl_8000805C accept
    #   Marth Fair against defender cap6/bone29 at Dash frame 2 after ShieldDesc misses.
    # - The current action's GuardOn submotion frame-0 capsules are far above the sword and miss;
    #   the same-frame frame-start Dash pose is the source-owned BODY publication.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800924C0
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ChangeMotionState}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/marth/VictoriousSpitefulAlpaca.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, contacts = _collect_contact_debug_for_row(dataset_path, 4317)
    defender = 0
    attacker = 1
    assert int(seed["action_id"][defender]) == 20  # Dash
    assert int(seed["action_id"][attacker]) == 60  # AttackAirF
    accepted = [
        c
        for c in contacts
        if int(c["attacker"]) == attacker
        and int(c["defender"]) == defender
        and int(c["hitbox_id"]) == 0
        and int(c["contact_kind"]) == 0
        and int(c["hurtcap_id"]) == 6
    ]
    assert len(accepted) == 1

    _seed, out, ref = _step_one_row(dataset_path, 4317)
    for field in ("action_id", "animation_index", "action_frame", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)


@pytest.mark.integration
def test_marth_attackairn_guardon_to_guard_dense_hitlist_suppresses_body_ipw_4229() -> None:
    # AttackAirN Ft_MF_SkipHit victim-list carry into the first steady Guard frame:
    # - p1 Marth NAir has a dense group-0 victim seed for p0's current Guard iid, while visible
    #   BODY attribution still names the same source port from an older attacker instance.
    # - GuardOn -> Guard does not clear that x914 HitCapsule victim ring on the source frame;
    #   lbColl_8000ACFC therefore rejects the hb0 BODY fallthrough after ShieldDesc misses.
    # - This lock is intentionally one-step only: stale dense seed provenance is not promoted into
    #   free-running HitCapsule state without a current victims-ring owner.
    # - Clearing only the dense group seed exposes the underlying geometry and reproduces the false
    #   DamageHi3 transition, proving the lock is hitlist ownership rather than a Guard pose fit.
    # refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_AttackAirN
    # refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_ProcessHit_8006D1EC}
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/marth/InternalPowerlessWallaby.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    attacker = 1
    defender = 0
    seed = ds.rows[4229:4230]["seed_t"].copy()
    assert int(seed[0]["action_id"][attacker]) == 65  # AttackAirN
    assert int(seed[0]["action_id"][defender]) == 179  # Guard
    assert int(seed[0]["seed_prev_action_id"][defender]) == 178  # GuardOn
    assert int(seed[0]["combat_hitlist_cd"][attacker, 0, defender]) == 0xFFFF
    assert int(seed[0]["combat_hitlist_victim_iid"][attacker, 0, defender]) == int(
        seed[0]["instance_id"][defender]
    )
    assert int(seed[0]["last_hit_by"][defender]) == int(seed[0]["source_port0"][attacker])
    assert int(seed[0]["instance_hit_by"][defender]) != int(seed[0]["instance_id"][attacker])

    out, ref = _step_one_row_with_seed(dataset_path, 4229, seed)
    for field in ("action_id", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)

    cleared = seed.copy()
    cleared[0]["combat_hitlist_cd"][attacker, 0, defender] = np.uint16(0)
    cleared[0]["combat_hitlist_victim_iid"][attacker, 0, defender] = np.uint16(0)
    out_without_latch, _ = _step_one_row_with_seed(dataset_path, 4229, cleared)
    assert int(out_without_latch["action_id"][defender]) == 77  # DamageHi3
    assert int(out_without_latch["hitlag"][defender]) > 0
    assert float(out_without_latch["percent"][defender]) > float(ref["percent"][defender])


@pytest.mark.integration
def test_marth_attackairn_stale_dense_guard_rows_still_allow_body_ipw_vsa() -> None:
    # Negative boundary for the Guard dense-latch owner:
    # - Continuing Guard rows can carry stale dense group state from older victim objects.
    # - If the defender iid is not the dense victim iid, or the previous visible action was already
    #   Guard, the source ring is not trusted to suppress a fresh BODY hit.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    cases = [
        ("InternalPowerlessWallaby.slpz", 464, 0, 76),  # continuing Guard, current iid seed.
        ("VictoriousSpitefulAlpaca.slpz", 5706, 0, 77),  # first Guard, stale victim iid.
    ]
    for dataset_name, record, defender, expected_action in cases:
        dataset_path = root / "replays/validation/marth" / dataset_name
        if not dataset_path.exists():
            pytest.skip(f"missing local replay: {dataset_path}")
        ds = load_replay_buffers(str(dataset_path))
        seed = ds.rows[record:record + 1]["seed_t"].copy()
        out, ref = _step_one_row_with_seed(dataset_path, record, seed)
        assert int(ref["action_id"][defender]) == expected_action
        for field in ("action_id", "hitlag", "hitstun", "instance_hit_by"):
            assert int(out[field][defender]) == int(ref[field][defender]), (
                f"{dataset_name}:{record}: field={field}"
            )
        assert float(out["percent"][defender]) == pytest.approx(
            float(ref["percent"][defender]), abs=1e-6
        )


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_name", "record", "defender", "expected_action"),
    [
        ("MotionlessAggressiveJay.slpz", 734, 1, 178),  # GuardOn stays no-hit.
        ("PriceyPartialAlbatross.slpz", 5142, 0, 181),  # GuardSetOff shield-hit path stays exact.
    ],
)
def test_guardon_frame_start_body_pose_does_not_broaden_adjacent_shield_rows(
    dataset_name: str, record: int, defender: int, expected_action: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/aggregate_recent" / dataset_name
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _step_one_row(dataset_path, record)
    assert int(seed["action_id"][defender]) in (20, expected_action)
    assert int(ref["action_id"][defender]) == expected_action
    for field in ("action_id", "animation_index", "action_frame", "hitlag", "hitstun", "instance_hit_by"):
        assert int(out[field][defender]) == int(ref[field][defender]), f"field={field}"
    assert float(out["percent"][defender]) == pytest.approx(float(ref["percent"][defender]), abs=1e-6)


@pytest.mark.integration
def test_marth_run_source_velocity_branch_keeps_body_pose_on_ipw_5292_hit_frame() -> None:
    # Source owner: ftCo_Run_Anim selects fp->gr_vel on ordinary floors and hidden mv.co.run.x4
    # only while ft_GetGroundFrictionMultiplier(fp) < 1.0. IPW:5292 has normal friction, so the
    # next ftAnim tick consumes the current gr_vel-derived rate; consuming the hidden source lane
    # instead crosses the next integer pose boundary and misses a source lbColl_8000805C BODY hit.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunDirect.c::ftCo_RunDirect_Anim
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / "replays/validation/marth/InternalPowerlessWallaby.slpz"
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    target_record = 5292
    defender = 0
    for rec in (target_record - 1, target_record, target_record + 1):
        assert int(samples.shape[0]) > rec, f"replay too short: record={rec}"

    target_seed = samples[target_record]["seed_t"]
    assert int(target_seed["action_id"][defender]) == 21  # Run
    assert int(target_seed["animation_index"][defender]) == 13  # Run submotion
    assert int(target_seed["action_frame"][defender]) == 1
    assert int(target_seed["on_ground"][defender]) == 1
    assert float(target_seed["ground_friction_mul"][defender]) == pytest.approx(1.0)
    assert abs(float(target_seed["run_anim_source_vel_f32"][defender])) > abs(
        float(target_seed["speed_ground_x_self"][defender])
    )

    for rec in (target_record - 1, target_record, target_record + 1):
        seed, out, ref = _step_one_row(dataset_path, rec)
        assert int(seed["action_id"][defender]) in (21, 90)
        for field in ("action_id", "animation_index", "action_frame", "hitlag", "hitstun", "instance_id"):
            assert int(out[field][defender]) == int(ref[field][defender]), (
                f"record={rec} field={field}"
            )
        assert float(out["percent"][defender]) == pytest.approx(
            float(ref["percent"][defender]), abs=1e-6
        )
