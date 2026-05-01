from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing


ACT_FALL = 29
ACT_DOWN_BOUND_D = 191


def _size(sizes: dict[str, int], key: str) -> int:
    if key in sizes:
        return int(sizes[key])
    return int(sizes[f"{key}_v0"])


def _run_one_step_row_ucf(ds_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(ds_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    sizes = binding.sizes()
    seed_stride = _size(sizes, "seed")
    input_stride = _size(sizes, "input")
    compare_stride = _size(sizes, "compare")
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])
    raw = samples.view(np.uint8).reshape(len(samples), -1)
    seed_bytes = np.array(raw[record : record + 1, seed_off : seed_off + seed_stride], copy=True)
    prev_bytes = np.array(raw[record : record + 1, prev_off : prev_off + input_stride], copy=True)
    input_bytes = np.array(raw[record : record + 1, input_off : input_off + input_stride], copy=True)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return (
        samples["seed_t"][record].copy(),
        samples["ref_t1"][record].copy(),
        out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy(),
    )


@pytest.mark.integration
def test_downbound_d_overlap_nudge_exits_fd_left_ledge_to_fall() -> None:
    # Replay-real positive for DownBound_Coll at FD's left ledge:
    # - DownBoundD receives an airborne-state script event that makes the collision callback's
    #   current frame a grounded candidate.
    # - ftCommon_8007E0E4 / ftCommon_8007DD7C applies the common x450 overlap nudge before
    #   DownBound_Coll consumes ft_80082708 -> mpColl_8004B108.
    # - The nudge pushes the root past the ledge floor endpoint, so the same callback enters Fall.
    # refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    # refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007DD7C,ftCommon_8007E0E4}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path.relative_to(root)}")

    seed, ref, out = _run_one_step_row_ucf(ds_path, 8904)
    p = 1
    assert int(seed["action_id"][p]) == ACT_DOWN_BOUND_D
    assert int(ref["action_id"][p]) == ACT_FALL
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert int(out["jumps_left"][p]) == int(ref["jumps_left"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6)


@pytest.mark.integration
def test_downbound_d_overlap_nudge_does_not_exit_before_ledge_boundary() -> None:
    # Neighboring negative: the same DownBoundD ledge episode remains airborne DownBound one frame
    # earlier. This keeps the x450 ledge-exit owner from becoming a broad DownBound/Fall shortcut.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds_path = root / "datasets/aggregate_recent/replays/validation/aggregate_recent/ImpassionedAlarmedTarsier.msl"
    if not ds_path.exists():
        pytest.skip(f"missing local dataset: {ds_path.relative_to(root)}")

    seed, ref, out = _run_one_step_row_ucf(ds_path, 8903)
    p = 1
    assert int(seed["action_id"][p]) == ACT_DOWN_BOUND_D
    assert int(ref["action_id"][p]) == ACT_DOWN_BOUND_D
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["on_ground"][p]) == int(ref["on_ground"][p]) == 0
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=2e-6)
