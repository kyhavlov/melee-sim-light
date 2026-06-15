from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _skip_if_marth_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/marth.json",
        "data/anims/marth.tracks.bin",
        "data/moves/marth.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local Marth data artifacts: {', '.join(missing)}")


def _run_one_step_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    return seed, ref, out


def _run_one_step_row_with_current_buttons(
    dataset_path: Path, record: int, player: int, buttons: int
) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_t = row["input_t"].copy()
    input_t["p"][0, player]["buttons"] = np.uint16(buttons)
    input_bytes = np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    return seed, ref, out


def _run_one_step_row_with_current_stick(
    dataset_path: Path, record: int, player: int, main_x: int, main_y: int
) -> tuple[np.void, np.void, np.void]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_t = row["input_t"].copy()
    input_t["p"][0, player]["main_x"] = np.int8(main_x)
    input_t["p"][0, player]["main_y"] = np.int8(main_y)
    input_bytes = np.frombuffer(input_t.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    return seed, ref, out


def _run_rollout_records(
    dataset_path: Path, start_record: int, records: tuple[int, ...]
) -> dict[int, tuple[np.void, np.void]]:
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > max(records), "dataset too short for rollout lock"
    assert start_record <= min(records), "rollout start must be <= first checked record"

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(
        samples[start_record : start_record + 1]["seed_t"].tobytes(order="C"), dtype=np.uint8
    ).copy().reshape(1, seed_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        out_by_record: dict[int, tuple[np.void, np.void]] = {}
        for rec in range(start_record, max(records) + 1):
            row = samples[rec : rec + 1]
            prev_input_bytes = np.frombuffer(
                row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
            ).copy().reshape(1, input_stride)
            input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
                1, input_stride
            )
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_compare_bytes)
            if rec in records:
                out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
                ref = row["ref_t1"].reshape(-1)[0].copy()
                out_by_record[rec] = (out, ref)
        return out_by_record
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_throw_release_mpcoll_floor_publication_mask_is_explicit_data() -> None:
    # The release-local mpColl floor publication owner is not "anchor part 88" as a runtime rule.
    # The data mask is an explicit probe-backed semantic: Marth ThrowF/ThrowLw and Sheik ThrowLw
    # publish, while Fox/Falco controls and Marth ThrowB do not.
    # Bit order: ThrowF, ThrowB, ThrowHi, ThrowLw.
    binding = pytest.importorskip("msl_binding")
    fox = binding.char_params_part_anchors(1)
    sheik = binding.char_params_part_anchors(7)
    marth = binding.char_params_part_anchors(18)
    falco = binding.char_params_part_anchors(22)

    assert int(fox["grab_capture_anchor_part_id"]) == 71
    assert int(sheik["grab_capture_anchor_part_id"]) == 56
    assert int(falco["grab_capture_anchor_part_id"]) == 65
    assert int(marth["grab_capture_anchor_part_id"]) == 88
    assert int(fox["throw_release_mpcoll_floor_publication_mask"]) == 0
    assert int(sheik["throw_release_mpcoll_floor_publication_mask"]) == (1 << 3)
    assert int(falco["throw_release_mpcoll_floor_publication_mask"]) == 0
    assert int(marth["throw_release_mpcoll_floor_publication_mask"]) == ((1 << 0) | (1 << 3))


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "start_record", "pre_record", "release_record", "post_records", "victim"),
    [
        (
            "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl",
            1347,
            1353,
            1354,
            (1355, 1356),
            1,
        ),
        (
            "datasets/marth/replays/validation/marth/ParallelFamiliarZebra.msl",
            1543,
            1549,
            1550,
            (1551, 1552),
            1,
        ),
        (
            "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl",
            10002,
            10008,
            10009,
            (10010, 10011),
            0,
        ),
    ],
)
def test_marth_throwf_release_uses_mpcoll_800471f8_substep_publication(
    dataset_rel: str, start_record: int, pre_record: int, release_record: int,
    post_records: tuple[int, ...], victim: int
) -> None:
    # Marth ThrowF/CaptureCut release publication:
    # - ftCo_800DDDE4 samples the throw-side TransN2 remap, writes x1A70, then calls
    #   mpColl_800471F8 before ftCo_800DE7C0 applies release damage.
    # - The mpColl_80043754 air callback publishes the floor-hit substep root, not the raw below-floor
    #   x1A70 target. Lock the source-owned action/Y handoff and keep X within the remaining
    #   upstream attached-pose drift instead of accepting the old raw-position DownBound path.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DDDE4,ftCo_800DE7C0}
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80043754,mpColl_80046904}
    root = Path(__file__).resolve().parents[1]
    _skip_if_marth_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    rows = _run_rollout_records(dataset_path, start_record, (pre_record, release_record, *post_records))
    out_pre, ref_pre = rows[pre_record]
    assert int(out_pre["action_id"][victim]) == int(ref_pre["action_id"][victim]) == 239

    out_release, ref_release = rows[release_record]
    assert int(ref_release["action_id"][victim]) == 88
    assert int(out_release["action_id"][victim]) == int(ref_release["action_id"][victim])
    assert float(out_release["pos_y"][victim]) == pytest.approx(
        float(ref_release["pos_y"][victim]), abs=4e-5
    )
    assert abs(float(out_release["pos_x"][victim]) - float(ref_release["pos_x"][victim])) <= 8e-3

    for rec in post_records:
        out_rec, ref_rec = rows[rec]
        assert int(out_rec["action_id"][victim]) == int(ref_rec["action_id"][victim]) == 88
        assert float(out_rec["pos_y"][victim]) == pytest.approx(float(ref_rec["pos_y"][victim]), abs=4e-5)
        assert abs(float(out_rec["pos_x"][victim]) - float(ref_rec["pos_x"][victim])) <= 8e-3


@pytest.mark.integration
def test_throwlw_release_non_marth_anchor_part_does_not_take_marth_mpcoll_publication() -> None:
    # Negative control from the regression surface:
    # Fox/Falco ftCo_800DDDE4 controls do not have the explicit throw-release mpColl publication
    # mask set, so grounded ThrownLw proceeds to DownBound instead of inheriting Marth's path.
    # data/characters/{fox,falco,marth}.json::throw_release_mpcoll_floor_publication_mask
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/FavorableSuperficialPig.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    victim = 1
    rows = _run_rollout_records(dataset_path, 9182, (9186, 9187, 9188))
    out_pre, ref_pre = rows[9186]
    assert int(out_pre["action_id"][victim]) == int(ref_pre["action_id"][victim]) == 242

    out_bound, ref_bound = rows[9187]
    assert int(ref_bound["action_id"][victim]) == 183
    assert int(out_bound["action_id"][victim]) == int(ref_bound["action_id"][victim])
    assert float(out_bound["pos_y"][victim]) == pytest.approx(float(ref_bound["pos_y"][victim]), abs=4e-5)

    out_after, ref_after = rows[9188]
    assert int(out_after["action_id"][victim]) == int(ref_after["action_id"][victim]) == 183


@pytest.mark.integration
def test_marth_throwb_release_anchor_part_does_not_publish_floor_sweep_result() -> None:
    # Direction negative from the IPW probe: Marth ThrowB also uses anchor part 88, but vanilla
    # returns the raw x1A70 target instead of publishing the substep floor line. This prevents the
    # part-remap owner from leaking into all throw directions.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80046904}
    root = Path(__file__).resolve().parents[1]
    _skip_if_marth_required_artifacts_missing(root)
    dataset_rel = "datasets/marth/replays/validation/marth/InternalPowerlessWallaby.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    victim = 0
    rows = _run_rollout_records(dataset_path, 8282, (8283, 8284, 8285))
    out_pre, ref_pre = rows[8283]
    assert int(out_pre["action_id"][victim]) == int(ref_pre["action_id"][victim]) == 240

    out_release, ref_release = rows[8284]
    assert int(ref_release["action_id"][victim]) == 88
    assert int(out_release["action_id"][victim]) == int(ref_release["action_id"][victim])
    assert int(out_release["ground_id"][victim]) == int(ref_release["ground_id"][victim]) == 5


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "start_record", "pre_terminal_record", "terminal_record", "owner", "terminal_action"),
    [
        (
            "datasets/marth/replays/validation/marth/StiffDraftyWalrus.msl",
            9073,
            9391,
            9392,
            0,
            14,
        ),
        (
            "datasets/marth/replays/validation/marth/FemaleWorthyAlpaca.msl",
            4155,
            4316,
            4317,
            1,
            15,
        ),
    ],
)
def test_marth_throwf_post_release_source_rate_reaches_anim_end(
    dataset_rel: str, start_record: int, pre_terminal_record: int, terminal_record: int,
    owner: int, terminal_action: int
) -> None:
    # Post-release ThrowF terminal owner:
    # - ftCo_800DD398 installs the victim-weight throw AObj rate.
    # - ftCo_800DD724 release detaches the victim, but does not reset the thrower's AObj rate.
    # - ftCo_ThrowF_Anim still exits through ftAnim_IsFramesRemaining on that source f32 rate.
    # The Q16 runtime timebase can land a few LSB below the extracted end frame; the terminal snap
    # is bounded to decoded post-release throw-owner states and must not pull the previous frame out
    # of ThrowF.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{
    #   ftCo_800DD398,ftCo_800DD724,ftCo_ThrowF_Anim}
    root = Path(__file__).resolve().parents[1]
    _skip_if_marth_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    rows = _run_rollout_records(dataset_path, start_record, (pre_terminal_record, terminal_record))
    out_pre, ref_pre = rows[pre_terminal_record]
    assert int(ref_pre["action_id"][owner]) == 219
    assert int(ref_pre["action_frame"][owner]) == 30
    assert int(out_pre["action_id"][owner]) == int(ref_pre["action_id"][owner])
    assert int(out_pre["action_frame"][owner]) == int(ref_pre["action_frame"][owner])

    out_terminal, ref_terminal = rows[terminal_record]
    assert int(ref_terminal["action_id"][owner]) == terminal_action
    assert int(out_terminal["action_id"][owner]) == int(ref_terminal["action_id"][owner])
    assert int(out_terminal["animation_index"][owner]) == int(ref_terminal["animation_index"][owner])
    assert float(out_terminal["pos_x"][owner]) == pytest.approx(float(ref_terminal["pos_x"][owner]), abs=4e-5)


@pytest.mark.integration
def test_throwhi_pending_release_placeholder_does_not_run_fall_phys_before_damage() -> None:
    # Aggregate F03 release-placement lock:
    # - ftCo_800DD724 consumes ThrowHi release during the thrower's Anim callback.
    # - ftCo_800DDDE4/ftCo_800DE7C0 then enter Damage before the victim's same-frame Phys, so the
    #   simulator's temporary Fall placeholder for deferred item ordering must not run generic Fall
    #   drift before the throw hit applies.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/HungryImportantSnake.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    victim = 1
    seed, ref, out = _run_one_step_row(dataset_path, 2419)
    assert int(seed["action_id"][victim]) == 241  # ThrownHi
    assert int(ref["action_id"][victim]) == 90  # DamageFlyTop
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim])
    assert float(out["pos_x"][victim]) == pytest.approx(float(ref["pos_x"][victim]), abs=2e-5)
    assert float(out["pos_y"][victim]) == pytest.approx(float(ref["pos_y"][victim]), abs=4e-5)

    rollout = _run_rollout_records(dataset_path, 2399, (2418, 2419, 2420, 2421))
    out_pre, ref_pre = rollout[2418]
    assert int(out_pre["action_id"][victim]) == int(ref_pre["action_id"][victim]) == 241
    assert float(out_pre["pos_x"][victim]) == pytest.approx(float(ref_pre["pos_x"][victim]), abs=1e-4)
    assert float(out_pre["pos_y"][victim]) == pytest.approx(float(ref_pre["pos_y"][victim]), abs=1e-4)

    for rec in (2419, 2420, 2421):
        out_rec, ref_rec = rollout[rec]
        assert int(out_rec["action_id"][victim]) == int(ref_rec["action_id"][victim]) == 90
        assert float(out_rec["pos_x"][victim]) == pytest.approx(float(ref_rec["pos_x"][victim]), abs=2e-5)
        assert float(out_rec["pos_y"][victim]) == pytest.approx(float(ref_rec["pos_y"][victim]), abs=4e-5)


@pytest.mark.integration
def test_throwhi_rollout_shared_throw_rate_reaches_release_frame_bhh() -> None:
    # Replay-real rollout lock for attached ThrowHi/ThrownHi timebase:
    # - ftCo_800DD4B0 computes the shared 4/3 throw anim rate for Fox/Fox ThrowHi.
    # - ftCo_800DD398 applies that rate to both thrower and victim before ftCo_800DD724's release
    #   script-frame gate.
    # - The Q16 runtime timebase must not land one LSB below the integer release frame and delay
    #   ThrownHi -> DamageFlyTop by one frame.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD4B0,ftCo_800DD398,ftCo_800DD724}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    out_by_record = _run_rollout_records(dataset_path, 467, (469, 472))
    out_469, ref_469 = out_by_record[469]
    out_472, ref_472 = out_by_record[472]

    victim_p = 0
    owner_p = 1
    assert int(out_469["action_id"][victim_p]) == int(ref_469["action_id"][victim_p]) == 241
    assert int(out_469["action_frame"][victim_p]) == int(ref_469["action_frame"][victim_p]) == 4
    assert int(out_469["action_frame"][owner_p]) == int(ref_469["action_frame"][owner_p]) == 4

    assert int(out_472["action_id"][victim_p]) == int(ref_472["action_id"][victim_p]) == 90
    assert int(out_472["animation_index"][victim_p]) == int(ref_472["animation_index"][victim_p])
    assert int(out_472["hitstun"][victim_p]) == int(ref_472["hitstun"][victim_p])


@pytest.mark.integration
@pytest.mark.parametrize("record", [1456, 7772])
def test_throw_release_pending_victim_strict_fields_match_ref(record: int) -> None:
    # Targeted lock rows in the ThrowF-release family:
    # - seed_t: victim starts in ThrownF on the release frame.
    # - ref_t1: victim transitions into DamageFlyN.
    # Lock strict replay-real parity on the stable ownership lanes affected by this slice.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    p = 0
    seed, ref, out = _run_one_step_row(dataset_path, record)

    assert int(seed["action_id"][p]) == 239
    assert int(seed["hitlag"][p]) == 1
    assert int(ref["action_id"][p]) == 88

    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "hitlag",
        "ground_id",
        "instance_id",
        "state_flags",
    ):
        if field == "state_flags":
            got = tuple(int(x) for x in out[field][p].tolist())
            exp = tuple(int(x) for x in ref[field][p].tolist())
            assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
        else:
            got = int(out[field][p])
            exp = int(ref[field][p])
            assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"


@pytest.mark.integration
@pytest.mark.parametrize("record", [1456, 7772])
def test_throw_release_pending_victim_position_context_shape(record: int) -> None:
    # Context-only position checks for the ThrowF release rows:
    # - strict lock keeps exact replay parity on discrete lanes,
    # - float lanes stay shape/stability checks until full replay parity is achievable.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > (record + 1), f"dataset too short for context row: record={record}"

    p = 0
    seed, ref, out = _run_one_step_row(dataset_path, record)
    next_seed = samples[record + 1]["seed_t"]

    # Replay continuity anchor for one-step vs rollout context:
    # seed_{t+1} is replay-real ref_{t+1} from row t.
    assert float(next_seed["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(next_seed["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)

    for lane in ("pos_x", "pos_y"):
        seed_v = float(seed[lane][p])
        out_v = float(out[lane][p])
        ref_v = float(ref[lane][p])

        assert np.isfinite(out_v), f"record={record} p={p} lane={lane} expected finite out"
        ref_delta = ref_v - seed_v
        out_delta = out_v - seed_v
        assert ref_delta != 0.0, f"record={record} p={p} lane={lane} expected non-zero replay delta"
        assert out_delta != 0.0, f"record={record} p={p} lane={lane} expected non-zero sim delta"
        assert np.sign(out_delta) == np.sign(
            ref_delta
        ), f"record={record} p={p} lane={lane} expected replay-consistent movement direction"


@pytest.mark.integration
@pytest.mark.parametrize(
    ("start_record", "target_record"),
    [
        (2178, 2181),
        (5578, 5581),
    ],
)
def test_throwf_owner_before_victim_release_uses_callback_local_transn_anchor_tch(
    start_record: int, target_record: int
) -> None:
    # Owner-before-victim grounded ThrowF release anchor:
    # - The thrower's Anim callback consumes `set_throw_flags(hit_idx=0)` before the victim
    #   callback runs in slot order.
    # - `ftCo_800DDDE4` samples FtPart_TransN2 before grounded ThrowF Phys consumes TransN through
    #   `ft_80085004/ft_80085030`, so this same-callback owner uses the callback-local TransN
    #   subtracted matrix for the release point.
    # - This is specifically owner slot < victim slot; owner-after-victim controls below stay on
    #   the established attached-release path.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowF_Anim,ftCo_800DD724,ftCo_800DDDE4}
    # refs/melee/src/melee/ft/ftparts.c::ft_80085004
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = (
        root
        / "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples["seed_t"][target_record]
    owner = 0
    victim = 1
    assert int(seed["action_id"][owner]) == 219  # ThrowF.
    assert int(seed["action_id"][victim]) == 239  # ThrownF.
    assert int(seed["action_frame"][owner]) == 10
    assert owner < victim

    out_by_record = _run_rollout_records(dataset_path, start_record, (target_record,))
    out, ref = out_by_record[target_record]

    assert int(ref["action_id"][victim]) == 88  # DamageFlyN.
    assert int(out["action_id"][victim]) == int(ref["action_id"][victim])
    assert int(out["action_frame"][victim]) == int(ref["action_frame"][victim])
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]) == 0
    assert float(out["pos_x"][victim]) == pytest.approx(float(ref["pos_x"][victim]), abs=2e-5)
    assert float(out["pos_y"][victim]) == pytest.approx(float(ref["pos_y"][victim]), abs=2e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "start_record", "target_record"),
    [
        (
            "datasets/aggregate_recent/replays/validation/aggregate_recent/BlondHardHippopotamus.msl",
            3404,
            3407,
        ),
        (
            "datasets/aggregate_recent/replays/validation/fountain_of_dreams_recent/"
            "MilkyGracefulStingray.msl",
            4485,
            4488,
        ),
    ],
)
def test_throwf_owner_after_victim_release_keeps_attached_anchor_controls(
    dataset_rel: str, start_record: int, target_record: int
) -> None:
    # Owner-after-victim negative controls for the ThrowF release anchor:
    # these rows have the victim slot before the thrower slot, so the victim callback has already
    # run by the time the thrower release script fires. They must not use the owner-before-victim
    # callback-local TransN-subtracted anchor.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    seed = ds.samples["seed_t"][target_record]
    victim = 0
    owner = 1
    assert int(seed["action_id"][owner]) == 219  # ThrowF.
    assert int(seed["action_id"][victim]) == 239  # ThrownF.
    assert victim < owner

    out_by_record = _run_rollout_records(dataset_path, start_record, (target_record,))
    out, ref = out_by_record[target_record]

    assert int(out["action_id"][victim]) == int(ref["action_id"][victim])
    assert int(out["action_frame"][victim]) == int(ref["action_frame"][victim])
    assert int(out["hitlag"][victim]) == int(ref["hitlag"][victim]) == 0
    assert float(out["pos_x"][victim]) == pytest.approx(float(ref["pos_x"][victim]), abs=2e-5)
    assert float(out["pos_y"][victim]) == pytest.approx(float(ref["pos_y"][victim]), abs=2e-6)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "ref_action_id", "max_pos_x_err", "max_pos_y_err"),
    [
        (1456, 88, 4.0, 0.021),
        (5717, 91, 4.0, 0.307),
        (7772, 88, 4.0, 0.425),
    ],
)
def test_throw_release_pending_victim_target_rows_keep_replay_real_discrete_and_tight_float_parity(
    record: int, ref_action_id: int, max_pos_x_err: float, max_pos_y_err: float
) -> None:
    # Replay-real target locks for the kept ThrowF release anchor lane:
    # - release-frame victim stays on the ref action/state shape exactly on the stable discrete
    #   ownership lanes,
    # - pos_x/pos_y are still one-step float residuals on the target rows, so keep the lock as a
    #   tight replay-relative tolerance with exact adjacent controls below.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    p = 0
    seed, ref, out = _run_one_step_row(dataset_path, record)
    assert int(seed["action_id"][p]) == 239
    assert int(ref["action_id"][p]) == ref_action_id
    assert int(out["action_id"][p]) == ref_action_id
    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "on_ground",
        "hitlag",
        "ground_id",
        "instance_id",
        "state_flags",
    ):
        if field == "state_flags":
            got = tuple(int(x) for x in out[field][p].tolist())
            exp = tuple(int(x) for x in ref[field][p].tolist())
            assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"
        else:
            assert int(out[field][p]) == int(ref[field][p]), (
                f"record={record} p={p} field={field} expected={int(ref[field][p])} "
                f"got={int(out[field][p])}"
            )
    assert abs(float(out["pos_x"][p]) - float(ref["pos_x"][p])) <= max_pos_x_err
    assert abs(float(out["pos_y"][p]) - float(ref["pos_y"][p])) <= max_pos_y_err
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]))


@pytest.mark.integration
def test_throw_release_immediate_di_does_not_apply_lr_lsi_multiplier() -> None:
    # Boundary lock for the no-hitlag throw-release path:
    # ftCo_800DE7C0 calls ftCo_8008E5A4, which applies DI from the Throw Anim callback's pre-input
    # stick direction. The L/R x1AC multiplier belongs to ftCo_Damage_OnExitHitlag, so holding L/R
    # must not scale the immediate throw-release KB magnitude.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    p = 0
    record = 1456
    no_lr_seed, no_lr_ref, no_lr_out = _run_one_step_row_with_current_buttons(dataset_path, record, p, 0)
    _, _, lr_out = _run_one_step_row_with_current_buttons(dataset_path, record, p, 0x0020 | 0x0040)

    assert int(no_lr_seed["action_id"][p]) == 239
    assert int(no_lr_seed["hitlag"][p]) == 1
    assert int(no_lr_ref["hitlag"][p]) == 0
    assert int(no_lr_ref["action_id"][p]) == 88
    assert int(no_lr_out["action_id"][p]) == int(lr_out["action_id"][p]) == 88

    no_lr_mag = float(np.hypot(float(no_lr_out["speed_x_attack"][p]), float(no_lr_out["speed_y_attack"][p])))
    lr_mag = float(np.hypot(float(lr_out["speed_x_attack"][p]), float(lr_out["speed_y_attack"][p])))
    assert lr_mag == pytest.approx(no_lr_mag, abs=1e-6)
    assert float(lr_out["speed_x_attack"][p]) == pytest.approx(float(no_lr_out["speed_x_attack"][p]), abs=1e-6)
    assert float(lr_out["speed_y_attack"][p]) == pytest.approx(float(no_lr_out["speed_y_attack"][p]), abs=1e-6)


@pytest.mark.integration
def test_throwhi_release_di_uses_pre_input_stick_lane() -> None:
    # Replay-real boundary for the GAT CaptureDamageLw -> ThrowHi release chain:
    # - ftCo_800DD724 runs as the Throw Anim callback before Fighter_procUpdate installs current
    #   input, then ftCo_800DE7C0 calls ftCo_8008E5A4 immediately.
    # - Release DI must therefore read the pre-input stick lane, not the already-applied current
    #   input_t lane; compatibility pending-release cleanup uses the same source lane.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008E5A4
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    record = 8412
    p = 1
    row = ds.samples[record]
    assert int(row["seed_t"]["action_id"][p]) == 241  # ThrownHi
    assert int(row["seed_t"]["action_frame"][p]) == 7
    assert int(row["ref_t1"]["action_id"][p]) == 90  # DamageFlyTop
    assert int(row["prev_input_t"]["p"]["main_x"][p]) == int(row["input_t"]["p"]["main_x"][p])
    assert int(row["prev_input_t"]["p"]["main_y"][p]) != int(row["input_t"]["p"]["main_y"][p])

    _, ref, out = _run_one_step_row(dataset_path, record)
    assert float(out["speed_x_attack"][p]) == pytest.approx(float(ref["speed_x_attack"][p]), abs=1e-5)
    assert float(out["speed_y_attack"][p]) == pytest.approx(float(ref["speed_y_attack"][p]), abs=1e-5)

    rollout = _run_rollout_records(dataset_path, 8395, (record,))
    rollout_out, rollout_ref = rollout[record]
    assert float(rollout_out["speed_x_attack"][p]) == pytest.approx(
        float(rollout_ref["speed_x_attack"][p]), abs=1e-5
    )
    assert float(rollout_out["speed_y_attack"][p]) == pytest.approx(
        float(rollout_ref["speed_y_attack"][p]), abs=1e-5
    )
    assert abs(float(rollout_out["pos_x"][p]) - float(rollout_ref["pos_x"][p])) <= 0.055
    assert abs(float(rollout_out["pos_y"][p]) - float(rollout_ref["pos_y"][p])) <= 0.005

    _, _, overridden = _run_one_step_row_with_current_stick(dataset_path, record, p, 0, 80)
    assert float(overridden["speed_x_attack"][p]) == pytest.approx(float(out["speed_x_attack"][p]), abs=1e-6)
    assert float(overridden["speed_y_attack"][p]) == pytest.approx(float(out["speed_y_attack"][p]), abs=1e-6)


@pytest.mark.integration
def test_throw_release_rollout_kb_decay_reaches_damagefly_floor_contact() -> None:
    # Runtime-dominant ThrowF release chain:
    # - release-frame throw KB is decayed before same-frame integration,
    # - the later DamageFlyTop floor contact reaches the decomp DownBound handoff instead of
    #   floating two frames high and missing the floor callback.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    out_by_record = _run_rollout_records(dataset_path, 265, (493, 497))

    p = 1
    out_air, ref_air = out_by_record[493]
    assert int(out_air["action_id"][p]) == int(ref_air["action_id"][p]) == 90
    assert int(out_air["on_ground"][p]) == int(ref_air["on_ground"][p]) == 0
    assert float(out_air["speed_y_attack"][p]) == pytest.approx(
        float(ref_air["speed_y_attack"][p]), abs=1e-6
    )

    out_land, ref_land = out_by_record[497]
    assert int(out_land["action_id"][p]) == int(ref_land["action_id"][p]) == 191
    assert int(out_land["on_ground"][p]) == int(ref_land["on_ground"][p]) == 1
    assert float(out_land["pos_y"][p]) == pytest.approx(float(ref_land["pos_y"][p]), abs=1e-6)
    assert float(out_land["speed_y_attack"][p]) == pytest.approx(
        float(ref_land["speed_y_attack"][p]), abs=1e-6
    )


@pytest.mark.integration
@pytest.mark.parametrize("record", [1455, 1457, 5716, 5718, 7771, 7773])
def test_throw_release_pending_adjacent_negative_controls_stay_replay_exact(record: int) -> None:
    # Explicit non-target controls for the broadened ThrowF anchor lane:
    # keep the pre-release ThrownF row and the post-release DamageFly continuation exact.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    p = 0
    _, ref, out = _run_one_step_row(dataset_path, record)
    for field in ("action_id", "action_frame", "animation_index", "hitlag", "on_ground", "instance_id"):
        assert int(out[field][p]) == int(ref[field][p]), (
            f"record={record} p={p} field={field} expected={int(ref[field][p])} "
            f"got={int(out[field][p])}"
        )
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-6)


@pytest.mark.integration
def test_throw_release_pending_context_controls_adjacent_rows_5716_5718() -> None:
    # Adjacent context controls around the targeted release row:
    # - pre row stays attached-thrown under hitlag
    # - post row remains in replay-real DamageFly continuation
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    p = 0

    seed, ref, out = _run_one_step_row(dataset_path, 5716)
    assert int(seed["action_id"][p]) == int(ref["action_id"][p]) == 239
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 1
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 0

    seed, ref, out = _run_one_step_row(dataset_path, 5718)
    assert int(seed["action_id"][p]) == int(ref["action_id"][p]) == 91
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p]) == 0
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p]) == 42
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0

    # Release row shape-control (known RNG-gated DamageFlyRoll lane):
    # keep the victim out of spurious air-locomotion entries on release.
    seed, ref, out = _run_one_step_row(dataset_path, 5717)
    assert int(seed["action_id"][p]) == 239
    assert int(ref["action_id"][p]) == 91
    assert int(out["action_id"][p]) in (88, 91)


@pytest.mark.integration
def test_throw_release_pending_context_row_owner_fsm_le1_extra_share_noop() -> None:
    # Context lock: ThrowF owner row with frame_speed_mul <= 1.0.
    # throw_flow_owner_throwf_deferred_extra_share() early-outs to 0.0 when
    # frame_speed_mul <= 1.0, so the ThrowF-specific extra ownership carry is a
    # no-op on this lane.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    # Replay-real context row: victim still attached-thrown under owner ThrowF, no release step yet.
    record = 216
    victim_p = 0
    owner_p = 1
    seed, ref, out = _run_one_step_row(dataset_path, record)

    assert int(seed["action_id"][victim_p]) == 239
    assert int(seed["action_id"][owner_p]) == 219
    assert int(seed["grab_owner_port"][victim_p]) == owner_p
    assert int(ref["action_id"][victim_p]) == 239

    frame_speed_mul = float(seed["frame_speed_mul_f32"][owner_p])
    # Function branch contract: no overspeed means no deferred ThrowF extra share.
    assert frame_speed_mul <= 1.0 + 1e-6

    owner_dx = float(seed["speed_ground_x_self"][owner_p])
    assert abs(owner_dx) > 1e-4

    assert int(out["action_id"][victim_p]) == int(ref["action_id"][victim_p])
    assert float(out["pos_x"][victim_p]) == pytest.approx(float(ref["pos_x"][victim_p]), abs=1e-6)
