from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_items_spawn_joint_replay_real_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset
from tools.eval.run_longest_rollout_streaks import _load_binding


_PPA = Path("datasets/aggregate_recent/replays/validation/aggregate_recent/PriceyPartialAlbatross.msl")


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


def _step_one(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = _load_binding()
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    samples_u8, seed_off, prev_input_off, input_off = _byte_views(ds)
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    input_bytes = np.empty((1, input_stride), dtype=np.uint8)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        seed_bytes[0, :] = samples_u8[record, seed_off : seed_off + seed_stride]
        prev_input_bytes[0, :] = samples_u8[record, prev_input_off : prev_input_off + input_stride]
        input_bytes[0, :] = samples_u8[record, input_off : input_off + input_stride]
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return samples["seed_t"][record], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy(), samples["ref_t1"][record]


def _rollout_rows(dataset_path: Path, start_record: int, end_record: int) -> dict[int, tuple[np.void, np.void, np.void]]:
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

    rows: dict[int, tuple[np.void, np.void, np.void]] = {}
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
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
def test_damageflyhi_right_wall_ecb_recenter_matches_ppa_direct_rows() -> None:
    # DamageFlyHi at FD's right wall uses mpColl_LoadECB_JObj's horizontal recenter before the
    # right-wall candidate/envelope pass. Without that source ECB shape, PPA 5576/5577 under-project
    # pos_x by ~0.29/~1.10 even though velocity lanes are already correct.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044E10_RightWall}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _PPA
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1
    for record in (5576, 5577, 5578):
        seed, out, ref = _step_one(dataset_path, record)
        assert int(seed["action_id"][p]) == 87  # DamageFlyHi
        assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 87
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6)
        assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=2e-6)


@pytest.mark.integration
def test_damageflyhi_push_only_wall_contact_does_not_promote_passivewall_rollout() -> None:
    # The first corrected right-wall contact is Push-only. Persisting that wall id into the next
    # frame must not manufacture a Hug bit and enter PassiveWall; Hug remains owned by the actual
    # side-point branch in mpColl_80044E10_RightWall.
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044E10_RightWall,mpColl_800454A4_RightWall}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _PPA
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")

    p = 1
    rows = _rollout_rows(dataset_path, 5567, 5583)
    out_5576, ref_5576, contacts_5576 = rows[5576]
    out_5577, ref_5577, contacts_5577 = rows[5577]
    out_5583, ref_5583, _contacts_5583 = rows[5583]

    assert int(out_5576["action_id"][p]) == int(ref_5576["action_id"][p]) == 87
    assert int(contacts_5576["wall_kind"][p]) == 2
    assert int(contacts_5576["wall_id"][p]) == 9
    assert int(contacts_5576["coll_env_flags"][p]) & 0x40
    assert not (int(contacts_5576["coll_env_flags"][p]) & 0x800)

    assert int(out_5577["action_id"][p]) == int(ref_5577["action_id"][p]) == 87
    assert int(contacts_5577["coll_prev_env_flags"][p]) & 0x40
    assert not (int(contacts_5577["coll_env_flags"][p]) & 0x800)

    assert int(out_5583["action_id"][p]) == int(ref_5583["action_id"][p]) == 252  # CliffCatch
    assert float(out_5583["pos_x"][p]) == pytest.approx(float(ref_5583["pos_x"][p]), abs=1e-6)
    assert float(out_5583["pos_y"][p]) == pytest.approx(float(ref_5583["pos_y"][p]), abs=2e-6)
