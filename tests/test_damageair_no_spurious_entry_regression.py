from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local extracted artifacts: {', '.join(missing)}")


def _step_one_record(*, binding, row: np.ndarray, num_players: int) -> np.ndarray:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(num_players))
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
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)


def _debug_knockdown_pre_physics_once(*, binding, row: np.ndarray, num_players: int) -> np.ndarray:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(num_players))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.debug_knockdown_update_pre_physics(handle)
        binding.write_compare(handle, out_compare_bytes)
        return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    p: int


_BASE = "replays/validation/cardinal_1.0_recent"
_MARTH_VALIDATION = "replays/validation/marth"
_DOUBLES_VALIDATION = "replays/validation/doubles_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _Case(f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 5369, 1),
        _Case(f"{_BASE}/GracefulAttachedTurtle.slpz", 2839, 0),
        _Case(f"{_BASE}/QuerulousGrandDinosaur.slpz", 753, 1),
        _Case(f"{_BASE}/TreasuredBackKangaroo.slpz", 2101, 0),
        _Case(f"{_MARTH_VALIDATION}/QuestionableHarmfulPanther.slpz", 8304, 1),
        _Case(f"{_DOUBLES_VALIDATION}/Game_20260509T152622.slpz", 3229, 3),
    ],
)
def test_damageair_anim_end_does_not_spuriously_stay_in_damageair(case: _Case) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > int(case.record), f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[case.record : case.record + 1]
    p = int(case.p)

    # Locked replay-real preconditions for this cluster:
    # - seed at t is DamageAir1 (84), ref at t+1 is Fall (29) with clean hitlag/hitstun.
    assert int(row["seed_t"]["action_id"][0, p]) == 84
    assert int(row["ref_t1"]["action_id"][0, p]) == 29
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["ref_t1"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["hitstun"][0, p]) == 0

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])


@pytest.mark.integration
def test_airborne_damageair_anim_end_exits_to_fall_when_x221c_b6_is_clear() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_BASE}/AttachedGoodNaturedGuanaco.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    record = 5369
    p = 1
    samples = ds.rows
    assert int(samples.shape[0]) > record, f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    # Replay-real airborne DamageAir anim-end family:
    # - ftCo_Damage_Anim gates both the airborne Fall exit and grounded Wait exit on !x221C_b6.
    # - No replay-real airborne anim-end row with x221C_b6 set exists in this local suite, so this
    #   lock covers the decomp-backed clear-bit ownership only.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
    assert int(row["seed_t"]["action_id"][0, p]) == 84
    assert int(row["seed_t"]["on_ground"][0, p]) == 0
    assert int(row["seed_t"]["action_frame"][0, p]) == 11
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert (int(row["seed_t"]["state_flags"][0, p, 3]) & 0x02) == 0
    assert int(row["ref_t1"]["action_id"][0, p]) == 29
    assert int(row["ref_t1"]["on_ground"][0, p]) == 0

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))

    for field in ("action_id", "animation_index", "on_ground", "action_frame"):
        got = int(out[field][p])
        exp = int(row["ref_t1"][field][0, p])
        assert got == exp, f"field={field} expected={exp} got={got}"


@pytest.mark.integration
def test_airborne_damageair_anim_end_x221c_b6_discriminator_blocks_v1_fall_branch() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_BASE}/AttachedGoodNaturedGuanaco.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    record = 5369
    p = 1
    samples = ds.rows
    assert int(samples.shape[0]) > record, f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1].copy()

    # Discriminator setup for V1 vs V2:
    # - start from the replay-real airborne DamageAir anim-end row,
    # - set the exported fp+0x221C high-byte bit1 (`x221C_b6`) while keeping hitstun clear,
    # - run only knockdown_update_pre_physics so the test targets the anim-end branch directly.
    #
    # Decomp ownership:
    # - ftCo_Damage_Anim gates the anim-end branch itself on !x221C_b6 before choosing grounded
    #   Wait vs airborne Fall.
    # - Therefore V1 (shared gate) must keep DamageAir here, while V2 (grounded-only gate) would
    #   incorrectly enter Fall on this constructed airborne row.
    # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    # refs/melee/src/melee/ft/types.h
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
    row["seed_t"]["state_flags"][0, p, 3] = np.uint8(int(row["seed_t"]["state_flags"][0, p, 3]) | 0x02)

    assert int(row["seed_t"]["action_id"][0, p]) == 84
    assert int(row["seed_t"]["on_ground"][0, p]) == 0
    assert int(row["seed_t"]["action_frame"][0, p]) == 11
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert (int(row["seed_t"]["state_flags"][0, p, 3]) & 0x02) != 0

    # Prove why the debug hook is needed:
    # - the normal step path runs timers_update_post_anim() before knockdown_update_pre_physics(),
    # - that pass clears the exported fp+0x221C hitstun bit when hitstun==0,
    # - so a full step no longer isolates the branch guard at src/knockdown.c:715.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
    full_step_out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(full_step_out["action_id"][p]) == 29

    # The debug hook isolates knockdown_update_pre_physics() itself, preserving the constructed
    # x221C_b6 input so V1 vs V2 branch ownership is directly testable.
    out = _debug_knockdown_pre_physics_once(binding=binding, row=row, num_players=int(ds.num_players))

    assert int(out["action_id"][p]) == 84
    assert int(out["animation_index"][p]) == 174
    assert int(out["on_ground"][p]) == 0
    assert int(out["action_frame"][p]) == 11
    assert int(out["hitstun"][p]) == 0
    assert (int(out["state_flags"][p, 3]) & 0x02) != 0


@pytest.mark.integration
def test_damageair_anim_end_same_frame_rehit_stays_damageair1() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_BASE}/TreasuredBackKangaroo.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    record = 2610
    p = 0
    samples = ds.rows
    assert int(samples.shape[0]) > record, f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    # Locked replay-real preconditions:
    # - seed at t is DamageAir1 (84) on its anim-end frame.
    # - replay ref at t+1 is still DamageAir1 because a same-frame hit re-enters damage.
    # Causal note (TBK rec=2610 p0): when laser BODY overlap is evaluated as a current-point probe
    # only, this frame misses the replay-causal same-frame laser contact and sim exits 84->29. With
    # prev->cur swept laser BODY overlap enabled, the contact is present and out stays in DamageAir1.
    assert int(row["seed_t"]["action_id"][0, p]) == 84
    assert int(row["seed_t"]["action_frame"][0, p]) == 11
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["action_id"][0, p]) == 84
    assert int(row["ref_t1"]["animation_index"][0, p]) == 174
    assert int(row["ref_t1"]["hitlag"][0, p]) == 3
    assert int(row["ref_t1"]["hitstun"][0, p]) == 9

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))

    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["animation_index"][p]) == int(row["ref_t1"]["animation_index"][0, p])


@pytest.mark.integration
@pytest.mark.parametrize("record", [2609, 2610, 2611])
def test_damageair_same_frame_body_sweep_family_with_adjacent_controls(record: int) -> None:
    # Replay-real lock for the same-frame BODY sweep family around TBK:2610:
    # - 2609: adjacent pre-control (no new BODY contact yet),
    # - 2610: target (same-frame BODY contact enters hitlag/hitstun while staying in DamageAir1),
    # - 2611: adjacent post-control (hitlag decay frame).
    #
    # Decomp collision ownership anchors:
    # - refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    # - refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    # - refs/melee/src/melee/it/itcoll.c::it_80272460
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = f"{_BASE}/TreasuredBackKangaroo.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = 0
    assert int(samples.shape[0]) > record, f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    def _laser_ids(items_row: np.ndarray) -> list[int]:
        out: list[int] = []
        for it in items_row:
            if int(it["exists"]) and int(it["type"]) in (54, 55):
                out.append(int(it["instance_id"]))
        out.sort()
        return out

    if record == 2609:
        assert int(row["seed_t"]["action_id"][0, p]) == 84
        assert int(row["ref_t1"]["action_id"][0, p]) == 84
        assert int(row["seed_t"]["action_frame"][0, p]) == 10
        assert int(row["ref_t1"]["action_frame"][0, p]) == 11
        assert int(row["seed_t"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 0
        assert int(row["seed_t"]["hitstun"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 0
    elif record == 2610:
        assert int(row["seed_t"]["action_id"][0, p]) == 84
        assert int(row["ref_t1"]["action_id"][0, p]) == 84
        assert int(row["seed_t"]["action_frame"][0, p]) == 11
        assert int(row["ref_t1"]["action_frame"][0, p]) == 1
        assert int(row["seed_t"]["hitlag"][0, p]) == 0
        assert int(row["ref_t1"]["hitlag"][0, p]) == 3
        assert int(row["seed_t"]["hitstun"][0, p]) == 0
        assert int(row["ref_t1"]["hitstun"][0, p]) == 9
    else:
        assert record == 2611
        assert int(row["seed_t"]["action_id"][0, p]) == 84
        assert int(row["ref_t1"]["action_id"][0, p]) == 84
        assert int(row["seed_t"]["action_frame"][0, p]) == 1
        assert int(row["ref_t1"]["action_frame"][0, p]) == 1
        assert int(row["seed_t"]["hitlag"][0, p]) == 3
        assert int(row["ref_t1"]["hitlag"][0, p]) == 2
        assert int(row["seed_t"]["hitstun"][0, p]) == 9
        assert int(row["ref_t1"]["hitstun"][0, p]) == 9

    ref_lasers = _laser_ids(row["ref_t1"]["items"][0])

    out = _step_one_record(binding=pytest.importorskip("msl_binding"), row=row, num_players=int(ds.num_players))

    for field in ("action_id", "animation_index", "on_ground", "hitlag", "hitstun", "action_frame"):
        got = int(out[field][p])
        exp = int(row["ref_t1"][field][0, p])
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"

    got_flags = [int(x) for x in out["state_flags"][p]]
    exp_flags = [int(x) for x in row["ref_t1"]["state_flags"][0, p]]
    assert got_flags == exp_flags, (
        f"record={record} p={p} field=state_flags expected={exp_flags} got={got_flags}"
    )

    got_lasers = _laser_ids(out["items"])
    assert got_lasers == ref_lasers, f"record={record} expected laser_ids={ref_lasers} got={got_lasers}"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case,expected_seed_action",
    [
        (_Case(f"{_BASE}/GracefulAttachedTurtle.slpz", 1892, 0), 84),
        (_Case(f"{_BASE}/AttachedGoodNaturedGuanaco.slpz", 2289, 1), 85),
    ],
)
def test_grounded_damageair_anim_end_exits_to_wait(case: _Case, expected_seed_action: int) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > int(case.record), f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[case.record : case.record + 1]
    p = int(case.p)

    # Replay-real grounded DamageAir anim-end family:
    # - ftCo_Damage_Anim enters Wait on anim end when the fighter is grounded and x221C_b6 is clear.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
    # refs/melee/src/melee/ft/ft_0892.c::ft_8008A2BC
    assert int(row["seed_t"]["action_id"][0, p]) == expected_seed_action
    assert int(row["seed_t"]["on_ground"][0, p]) == 1
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["ref_t1"]["action_id"][0, p]) == 14
    assert int(row["ref_t1"]["animation_index"][0, p]) == 2
    assert int(row["ref_t1"]["on_ground"][0, p]) == 1

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))

    for field in ("action_id", "animation_index", "on_ground", "action_frame"):
        got = int(out[field][p])
        exp = int(row["ref_t1"][field][0, p])
        assert got == exp, f"field={field} expected={exp} got={got}"


@pytest.mark.integration
@pytest.mark.parametrize("record,expected_action,expected_grounded", [(924, 82, 0), (925, 42, 1)])
def test_common_damage_terminal_fall_static_platform_handoff_pfz(
    record: int, expected_action: int, expected_grounded: int
) -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    if not (root / "data/characters/marth.json").exists():
        pytest.skip("missing local extracted artifact: data/characters/marth.json")
    dataset_rel = f"{_MARTH_VALIDATION}/ParallelFamiliarZebra.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    p = 0
    assert int(samples.shape[0]) > record, f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    # Replay-real terminal common Damage -> Fall collision boundary:
    # - rec924 is the adjacent pre-terminal DamageLw2 control and must stay airborne.
    # - rec925 has Damage_Anim entering Fall before map collision, while ftCo_Fall_Enter preserves
    #   the previous Damage submotion for this callback; Fall_Coll then admits the carried static
    #   Battlefield platform through the normal one-way-platform stick gate and enters Landing.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_Coll}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047F40,mpColl_80044628_Floor,mpColl_80044838_Floor}
    assert int(row["seed_t"]["action_id"][0, p]) == 82
    assert int(row["seed_t"]["animation_index"][0, p]) == 172
    assert int(row["seed_t"]["hitlag"][0, p]) == 0
    assert int(row["seed_t"]["hitstun"][0, p]) == 0
    assert int(row["seed_t"]["ground_id"][0, p]) == 2
    assert int(row["input_t"]["p"][0, p]["main_y"]) > -72
    assert int(row["ref_t1"]["action_id"][0, p]) == expected_action
    assert int(row["ref_t1"]["on_ground"][0, p]) == expected_grounded

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))

    for field in ("action_id", "animation_index", "on_ground", "ground_id", "jumps_left", "action_frame"):
        got = int(out[field][p])
        exp = int(row["ref_t1"][field][0, p])
        assert got == exp, f"record={record} field={field} expected={exp} got={got}"

    assert float(out["pos_y"][p]) == pytest.approx(float(row["ref_t1"]["pos_y"][0, p]), abs=1e-5)


@pytest.mark.integration
def test_common_damage_terminal_fall_static_platform_handoff_respects_pass_stick_pfz() -> None:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    if not (root / "data/characters/marth.json").exists():
        pytest.skip("missing local extracted artifact: data/characters/marth.json")
    dataset_rel = f"{_MARTH_VALIDATION}/ParallelFamiliarZebra.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    record = 925
    p = 0
    samples = ds.rows
    assert int(samples.shape[0]) > record, f"replay too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1].copy()

    # Synthetic source-shaped negative: the same terminal Damage/Fall row must not land on the
    # carried static platform when the source platform-pass stick gate rejects the floor.
    # refs/melee/src/melee/mp/mpcoll.c::mpColl_80044628_Floor platform callback path
    row["input_t"]["p"][0, p]["main_y"] = np.int8(-127)

    out = _step_one_record(binding=binding, row=row, num_players=int(ds.num_players))

    assert int(out["action_id"][p]) == 29
    assert int(out["animation_index"][p]) == 20
    assert int(out["on_ground"][p]) == 0
    assert float(out["pos_y"][p]) < float(row["ref_t1"]["pos_y"][0, p])
