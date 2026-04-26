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
        binding.reseed_seed(handle, seed_bytes)
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
        binding.reseed_seed(handle, seed_bytes)
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
    # - This simulator defers throw-hit application until post-items, so the DI source must remain
    #   the pre-input stick lane, not the already-applied current input_t lane.
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
