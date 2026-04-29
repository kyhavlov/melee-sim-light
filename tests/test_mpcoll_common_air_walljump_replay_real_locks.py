from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_items_spawn_joint_replay_real_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding


_HVG = Path("datasets/aggregate_recent/replays/validation/aggregate_recent/HilariousVillainousGiraffe.msl")


def _contact_dtype() -> np.dtype:
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


def _byte_views(ds):
    samples = ds.samples
    stride = int(samples.dtype.itemsize)
    u8 = samples.view(np.uint8).reshape(int(samples.shape[0]), stride)
    return (
        u8,
        int(samples.dtype.fields["seed_t"][1]),
        int(samples.dtype.fields["prev_input_t"][1]),
        int(samples.dtype.fields["input_t"][1]),
    )


def _rollout_rows(dataset_path: Path, start_record: int, end_record: int):
    binding = _load_binding()
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    samples_u8, seed_off, prev_input_off, input_off = _byte_views(ds)
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contact_dtype = _contact_dtype()

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    contact_bytes = np.zeros((1, contact_dtype.itemsize), dtype=np.uint8)

    rows = {}
    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes[0, :] = samples_u8[start_record, seed_off : seed_off + seed_stride]
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, end_record + 1):
            prev_input_bytes[0, :] = samples_u8[
                record, prev_input_off : prev_input_off + input_stride
            ]
            input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
            binding.step_input(handle, prev_input_bytes, input_bytes)
            binding.write_compare(handle, out_bytes)
            binding.debug_write_collision_contacts(handle, contact_bytes)
            rows[record] = (
                out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(),
                samples["ref_t1"][record].copy(),
                contact_bytes.view(contact_dtype).reshape(-1)[0].copy(),
            )
    finally:
        binding.destroy(handle)
    return rows


@pytest.mark.integration
def test_common_air_left_walljump_uses_frame_start_pos_delta_for_setup() -> None:
    # HVG 4666 has a live left-wall Hug after mpColl projection, but fp->pos_delta.x from
    # Fighter_8006A360 is still below Falco co_attrs.x148, so ftWallJump_8008169C must only start
    # the hidden setup phase. Using post-collision displacement enters PassiveWallJump one frame
    # early. On the next frame, the frame-start pos_delta is large enough and the rollout enters.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _HVG
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    rows = _rollout_rows(dataset_path, 4650, 4672)
    p = 0
    got_4666, ref_4666, contacts_4666 = rows[4666]
    got_4667, ref_4667, contacts_4667 = rows[4667]
    got_4672, ref_4672, contacts_4672 = rows[4672]

    assert int(contacts_4666["wall_kind"][p]) == 1
    assert int(contacts_4666["wall_id"][p]) == 13
    assert int(contacts_4666["coll_env_flags"][p]) & 0x20
    assert int(got_4666["action_id"][p]) == int(ref_4666["action_id"][p]) == 27
    assert float(got_4666["pos_x"][p]) == pytest.approx(float(ref_4666["pos_x"][p]), abs=1e-6)

    assert int(contacts_4667["wall_kind"][p]) == 1
    assert int(contacts_4667["wall_id"][p]) == 13
    assert int(contacts_4667["coll_env_flags"][p]) & 0x20
    assert int(got_4667["action_id"][p]) == int(ref_4667["action_id"][p]) == 203
    assert float(got_4667["pos_x"][p] - ref_4667["pos_x"][p]) == pytest.approx(
        0.29499054, abs=1e-6
    )

    assert int(got_4672["action_id"][p]) == int(ref_4672["action_id"][p]) == 203
    assert int(contacts_4672["wall_id"][p]) == 13
    assert float(got_4672["pos_x"][p] - ref_4672["pos_x"][p]) == pytest.approx(
        0.29499054, abs=1e-6
    )

