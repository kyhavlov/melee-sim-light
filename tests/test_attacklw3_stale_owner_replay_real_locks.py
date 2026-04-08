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
    row = ds.samples[record : record + 1]
    assert int(row.shape[0]) == 1

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

    return row["seed_t"][0], row["ref_t1"][0], out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]


def _assert_f32_bits_match(got: float, exp: float, *, record: int, p: int, field: str) -> None:
    got_bits = int(np.float32(got).view(np.uint32))
    exp_bits = int(np.float32(exp).view(np.uint32))
    assert got_bits == exp_bits, (
        f"record={record} p={p} field={field} expected={exp} (0x{exp_bits:08x}) "
        f"got {got} (0x{got_bits:08x})"
    )


@pytest.mark.integration
@pytest.mark.parametrize("record", [7077, 7078, 7079])
def test_attacklw3_stale_owner_target_and_adjacent_rows_stay_replay_exact(record: int) -> None:
    # Replay-real locks for the kept AttackLw3 stale-owner lane:
    # - AttackLw3 keeps same-group create_hitbox ownership across the continuing damage window.
    # - Dense seed hitlists can suppress the real followup BODY hit one frame late unless the stale
    #   owner is cleared on the continuing damage row.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_Anim
    # refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    p = 1
    seed, ref, out = _run_one_step_row(dataset_path, record)

    if record == 7078:
        assert int(seed["action_id"][p]) == 86
        assert int(ref["action_id"][p]) == 90
    else:
        assert int(ref["action_id"][p]) in (86, 90)

    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "hitlag",
        "hitstun",
        "instance_id",
    ):
        assert int(out[field][p]) == int(ref[field][p]), (
            f"record={record} p={p} field={field} expected={int(ref[field][p])} got={int(out[field][p])}"
        )

    assert [int(x) for x in out["state_flags"][p]] == [int(x) for x in ref["state_flags"][p]]
    _assert_f32_bits_match(float(out["percent"][p]), float(ref["percent"][p]), record=record, p=p, field="percent")
    _assert_f32_bits_match(float(out["pos_x"][p]), float(ref["pos_x"][p]), record=record, p=p, field="pos_x")
    _assert_f32_bits_match(float(out["pos_y"][p]), float(ref["pos_y"][p]), record=record, p=p, field="pos_y")


@pytest.mark.integration
def test_attacklw3_stale_owner_negative_control_stays_replay_exact() -> None:
    # Explicit non-target control for the AttackLw3 stale-owner branch:
    # a preceding continuing-damage row in the same local family must stay replay-real.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = 7076
    p = 1
    seed, ref, out = _run_one_step_row(dataset_path, record)
    assert int(seed["action_id"][p]) == 86
    assert int(ref["action_id"][p]) == 86

    for field in ("action_id", "action_frame", "animation_index", "hitlag", "hitstun", "instance_id"):
        assert int(out[field][p]) == int(ref[field][p]), (
            f"record={record} p={p} field={field} expected={int(ref[field][p])} got={int(out[field][p])}"
        )
    assert [int(x) for x in out["state_flags"][p]] == [int(x) for x in ref["state_flags"][p]]
    _assert_f32_bits_match(float(out["percent"][p]), float(ref["percent"][p]), record=record, p=p, field="percent")

