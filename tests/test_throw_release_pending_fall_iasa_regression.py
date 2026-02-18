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
