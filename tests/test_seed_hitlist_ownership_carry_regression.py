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


def _parse_field(field: str) -> tuple[str, int | None]:
    if "[" in field and field.endswith("]"):
        base, rest = field.split("[", 1)
        return base, int(rest[:-1])
    return field, None


def _scalar_seed_or_ref(row_obj: np.ndarray, *, p: int, field: str) -> int:
    base, sub = _parse_field(field)
    if sub is None:
        return int(row_obj[base][p])
    return int(row_obj[base][p, sub])


def _scalar_out(row_obj: np.ndarray, *, p: int, field: str) -> int:
    base, sub = _parse_field(field)
    if sub is None:
        return int(row_obj[base][p])
    return int(row_obj[base][p, sub])


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


def _assert_fields_match_ref(dataset_path: Path, *, record: int, p: int, fields: tuple[str, ...]) -> None:
    seed, ref, out = _run_one_step_row(dataset_path, record)
    for field in fields:
        got = _scalar_out(out, p=p, field=field)
        exp = _scalar_seed_or_ref(ref, p=p, field=field)
        assert got == exp, f"record={record} p={p} field={field} expected={exp} got={got}"


@pytest.mark.integration
def test_seed_hitlist_ownership_carry_agg_1430_family_context_and_seed_blocker_lock() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    # Target row preconditions.
    seed, ref, _ = _run_one_step_row(dataset_path, 1430)
    p = 1
    assert int(seed["action_id"][p]) == 178
    assert int(ref["action_id"][p]) == 181
    assert int(seed["hitlag"][p]) == 0
    assert int(ref["hitlag"][p]) == 5

    # Adjacent controls keep the transition context stable.
    seed_pre, ref_pre, _ = _run_one_step_row(dataset_path, 1429)
    assert int(seed_pre["action_id"][p]) == int(ref_pre["action_id"][p]) == 178
    assert int(seed_pre["hitlag"][p]) == int(ref_pre["hitlag"][p]) == 0

    seed_post, ref_post, _ = _run_one_step_row(dataset_path, 1431)
    assert int(seed_post["action_id"][p]) == int(ref_post["action_id"][p]) == 181
    assert int(seed_post["hitlag"][p]) == 5
    assert int(ref_post["hitlag"][p]) == 4

    # Seed-side blocker context remains observable on this row and is kept as a replay-real lock.
    assert int(seed["combat_hitlist_cd"][0, 0, 1]) == 0xFFFF
    assert int(seed["combat_hitlist_victim_iid"][0, 0, 1]) == 281

    # Causal Illusion context lock for this family:
    # - rec=1430 seeds one Falco Phantasm item (ItKind=57) owned by p0.
    # - Seed materialization carries ghost index-1 position (owner prior-frame world position).
    # - Adjacent control verifies timer/instance continuity at rec=1431.
    items_mid = seed["items"]
    mid_live = [i for i in range(items_mid.shape[0]) if int(items_mid[i]["exists"]) != 0]
    assert mid_live == [0]
    it_mid = items_mid[mid_live[0]]
    assert int(it_mid["type"]) == 57
    assert int(it_mid["state"]) == 0
    assert int(it_mid["owner"]) == 0
    assert int(seed_pre["action_id"][0]) == 348
    assert float(it_mid["pos_x"]) == pytest.approx(float(seed_pre["pos_x"][0]), abs=1e-6)
    assert float(it_mid["pos_y"]) == pytest.approx(float(seed_pre["pos_y"][0]), abs=1e-6)

    items_post = seed_post["items"]
    post_live = [i for i in range(items_post.shape[0]) if int(items_post[i]["exists"]) != 0]
    assert post_live == [0]
    it_post = items_post[post_live[0]]
    assert int(it_post["type"]) == 57
    assert int(it_post["owner"]) == 0
    assert int(it_post["instance_id"]) == int(it_mid["instance_id"])
    assert float(it_post["timer"]) == pytest.approx(float(it_mid["timer"]) - 1.0, abs=1e-6)

    fields = ("action_id", "action_frame", "animation_index", "hitlag", "hitstun", "instance_id", "state_flags[1]")
    _assert_fields_match_ref(dataset_path, record=1429, p=1, fields=fields)
    _assert_fields_match_ref(dataset_path, record=1430, p=1, fields=fields)
    _assert_fields_match_ref(dataset_path, record=1431, p=1, fields=fields)
    _assert_fields_match_ref(dataset_path, record=1430, p=0, fields=("action_id", "hitlag", "hitstun"))


@pytest.mark.integration
def test_seed_hitlist_ownership_carry_agg_848_family_replay_real_locks() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    # Target-frame preconditions: both players should take hitlag on rec=848.
    seed, ref, _ = _run_one_step_row(dataset_path, 848)
    assert int(seed["hitlag"][0]) == 0
    assert int(seed["hitlag"][1]) == 0
    assert int(ref["hitlag"][0]) == 6
    assert int(ref["hitlag"][1]) == 6

    # Adjacent controls around the target lane.
    seed_pre, ref_pre, _ = _run_one_step_row(dataset_path, 847)
    assert int(seed_pre["hitlag"][0]) == int(ref_pre["hitlag"][0]) == 0
    assert int(seed_pre["hitlag"][1]) == int(ref_pre["hitlag"][1]) == 0

    seed_post, ref_post, _ = _run_one_step_row(dataset_path, 849)
    assert int(seed_post["hitlag"][0]) == 6
    assert int(ref_post["hitlag"][0]) == 5
    assert int(seed_post["hitlag"][1]) == 6
    assert int(ref_post["hitlag"][1]) == 5

    fields = ("action_id", "action_frame", "animation_index", "hitlag", "hitstun", "instance_id")
    for rec in (847, 848, 849):
        for p in (0, 1):
            _assert_fields_match_ref(dataset_path, record=rec, p=p, fields=fields)
