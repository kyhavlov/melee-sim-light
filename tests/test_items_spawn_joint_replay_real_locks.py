from __future__ import annotations

import json
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
        "data/items/lasers.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local artifacts: {', '.join(missing)}")


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    assert int(samples.shape[0]) > int(record), f"replay too short for lock row: record={record}"
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

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    return seed, out, ref


def _read_spawn_joint_part_id(root: Path, *, character: str) -> int:
    path = root / "data" / "characters" / f"{character}.json"
    payload = json.loads(path.read_text())
    return int(payload.get("laser_spawn_joint_part_id", 0))


@dataclass(frozen=True)
class _SpawnCase:
    dataset_rel: str
    record: int
    slot: int
    shot_itkind: int
    owner_port: int
    owner_char_id: int
    owner_char_name: str
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _SpawnCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz",
            record=136,
            slot=1,
            shot_itkind=55,
            owner_port=0,
            owner_char_id=22,
            owner_char_name="falco",
            note="Falco laser spawn lock",
        ),
        _SpawnCase(
            dataset_rel="replays/validation/cardinal_1.0_recent/QuerulousGrandDinosaur.slpz",
            record=3239,
            slot=1,
            shot_itkind=54,
            owner_port=1,
            owner_char_id=1,
            owner_char_name="fox",
            note="Fox laser spawn lock",
        ),
    ],
)
def test_laser_spawn_joint_lane_rows_match_replay_real(case: _SpawnCase) -> None:
    # Replay-real lock for spawn-joint lane wiring:
    # - runtime laser spawn joint must come from extracted character attr lane
    #   `laser_spawn_joint_part_id` (PlCo ftPartsTable-derived),
    # - spawned item identity + world position must match replay at t+1.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_FtGetHoldJoint
    # refs/melee/src/melee/ft/ftparts.c::ftParts_GetBoneIndex
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    joint_part = _read_spawn_joint_part_id(root, character=case.owner_char_name)
    assert joint_part > 0, case.note

    seed, out, ref = _step_one_row(dataset_path, case.record)
    slot = int(case.slot)

    assert int(seed["items"][slot]["exists"]) == 0, case.note
    assert int(ref["items"][slot]["exists"]) == 1, case.note
    assert int(ref["items"][slot]["type"]) == int(case.shot_itkind), case.note
    assert int(ref["items"][slot]["owner"]) == int(case.owner_port), case.note
    assert int(seed["char_id"][case.owner_port]) == int(case.owner_char_id), case.note

    for fld in ("exists", "type", "owner", "instance_id", "state", "direction"):
        assert int(out["items"][slot][fld]) == int(ref["items"][slot][fld]), f"{case.note}: field={fld}"

    for fld in ("pos_x", "pos_y", "vel_x", "vel_y"):
        got = np.float32(out["items"][slot][fld])
        exp = np.float32(ref["items"][slot][fld])
        assert abs(float(got) - float(exp)) <= 1e-6, (
            f"{case.note}: field={fld} expected={float(exp)} got={float(got)}"
        )
