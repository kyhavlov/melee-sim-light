from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _one_step(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[record : record + 1]
    assert int(row.shape[0]) == 1

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = (
            np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, seed_stride)
            .copy()
        )
        prev_input_bytes = (
            np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, input_stride)
            .copy()
        )
        input_bytes = (
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, input_stride)
            .copy()
        )
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        return row["seed_t"][0], out_bytes.view(COMPARE_DTYPE).reshape(-1)[0], row["ref_t1"][0]
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _ParityCase:
    dataset_rel: str
    record: int
    player: int
    item_slot: int
    note: str


_AGG = "replays/validation/aggregate_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _ParityCase(
            dataset_rel=f"{_AGG}/BlondHardHippopotamus.slpz",
            record=640,
            player=0,
            item_slot=1,
            note="adjacent DownBound laser row before trailing-scale contact remains alive",
        ),
        _ParityCase(
            dataset_rel=f"{_AGG}/BlondHardHippopotamus.slpz",
            record=641,
            player=0,
            item_slot=1,
            note="DownBound trailing laser BODY uses previous x58 scale and stays alive",
        ),
        _ParityCase(
            dataset_rel=f"{_AGG}/ImpassionedAlarmedTarsier.slpz",
            record=1791,
            player=1,
            item_slot=0,
            note="adjacent airborne laser row before x58 scale correction remains alive",
        ),
        _ParityCase(
            dataset_rel=f"{_AGG}/ImpassionedAlarmedTarsier.slpz",
            record=1792,
            player=1,
            item_slot=0,
            note="airborne laser BODY uses previous x58 scale before full hit",
        ),
        _ParityCase(
            dataset_rel=f"{_AGG}/ImpassionedAlarmedTarsier.slpz",
            record=1793,
            player=1,
            item_slot=0,
            note="following airborne laser full BODY hit still consumes",
        ),
    ],
    ids=lambda case: case.note,
)
def test_laser_hitcap_prev_scale_replay_rows(case: _ParityCase) -> None:
    # Replay-real locks for laser HitCapsule x58/x4C scale ownership:
    # - it_8027137C carries previous x4C into x58, then rebuilds current x4C.
    # - itFoxlaser_UnkMotion1_Anim advances the laser scale once per item anim update, so x58 uses
    #   the previous post-frame scale while x4C uses the current post-anim scale.
    # refs/melee/src/melee/it/itcoll.c::it_8027137C
    # refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    seed, out, ref = _one_step(dataset_path, case.record)
    p = case.player
    slot = case.item_slot
    assert int(seed["items"][slot]["exists"]) == 1, case.note
    assert int(seed["items"][slot]["type"]) in (54, 55), case.note

    for field in (
        "action_id",
        "action_frame",
        "animation_index",
        "hitlag",
        "hitstun",
        "instance_hit_by",
        "instance_id",
        "last_hit_by",
    ):
        assert int(out[field][p]) == int(ref[field][p]), f"{case.note}: {field}"
    assert float(out["percent"][p]) == pytest.approx(float(ref["percent"][p]), abs=1e-6), case.note
    assert [int(x) for x in out["state_flags"][p]] == [int(x) for x in ref["state_flags"][p]]

    for field in ("exists", "state", "type", "owner", "instance_id"):
        assert int(out["items"][slot][field]) == int(ref["items"][slot][field]), (
            f"{case.note}: item {field}"
        )


def test_laser_hitcap_prev_scale_does_not_enable_rejected_unscaled_offset_fallback() -> None:
    # Negative control for the rejected F16d unscaled-offset fallback. The previous-scale fix must
    # not admit adjacent GAT laser rows through a broad unscaled BODY offset; the later GAT:7215
    # consume is covered by the separate lbColl hurt-radius lane.
    root = Path(__file__).resolve().parents[1]
    dataset_path = (
        root
        / "replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_path}")

    seed, out, ref = _one_step(dataset_path, 7214)
    p = 0
    slot = 0
    assert int(seed["action_id"][p]) == 20  # Dash
    assert int(seed["items"][slot]["type"]) == 55
    assert int(ref["items"][slot]["exists"]) == 1
    assert int(out["items"][slot]["exists"]) == 1
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 20
