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


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p", "seed_action", "ref_action", "seed_hitlag", "ref_hitlag"),
    [
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl",
            1889,
            1,
            0,  # DeadDown
            12,  # Rebirth
            0,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/TreasuredBackKangaroo.msl",
            444,
            1,
            241,  # ThrownHi
            90,  # DamageFlyTop
            0,
            0,
        ),
        (
            "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/GracefulAttachedTurtle.msl",
            1456,
            0,
            239,  # ThrownF
            88,  # DamageFlyN
            1,
            0,
        ),
    ],
)
def test_instance_id_transition_clusters_match_ref(
    dataset_rel: str,
    record: int,
    p: int,
    seed_action: int,
    ref_action: int,
    seed_hitlag: int,
    ref_hitlag: int,
) -> None:
    # Regression lock for core instance_id transitions:
    # - Dead* -> Rebirth call-chain consumes one hidden plAttack_80037B08 lane before ft_800895E0.
    # - Throw release fallback uses timebase restart (not full motion identity bundle).
    # refs/melee/src/melee/pl/plattack.c::plAttack_80037B08
    # refs/melee/build/GALE01/asm/melee/ft/fighter.s::Fighter_UnkProcessDeath_80068354
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > record, f"dataset too short: num_records={int(samples.shape[0])}"
    row = samples[record : record + 1]

    assert int(row["seed_t"]["action_id"][0, p]) == seed_action
    assert int(row["ref_t1"]["action_id"][0, p]) == ref_action
    assert int(row["seed_t"]["hitlag"][0, p]) == seed_hitlag
    assert int(row["ref_t1"]["hitlag"][0, p]) == ref_hitlag
    assert int(row["seed_t"]["hitstun"][0, p]) == 0

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
        got_action = int(out["action_id"][0, p])
        got_iid = int(out["instance_id"][0, p])
        want_action = int(row["ref_t1"]["action_id"][0, p])
        want_iid = int(row["ref_t1"]["instance_id"][0, p])

        assert got_action == want_action, (
            f"record={record} p={p} expected action_id={want_action}, got {got_action}"
        )
        assert got_iid == want_iid, (
            f"record={record} p={p} expected instance_id={want_iid}, got {got_iid}"
        )
    finally:
        binding.destroy(handle)
