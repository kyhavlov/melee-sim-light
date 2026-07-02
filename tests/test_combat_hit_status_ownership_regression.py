from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


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


def _step_one_row(dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
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
        prev_input_bytes = np.frombuffer(
            row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(1, input_stride).copy()
        input_bytes = (
            np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8)
            .reshape(1, input_stride)
            .copy()
        )
        out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)

        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
        seed = row["seed_t"][0]
        ref = row["ref_t1"][0]
        return seed, out, ref
    finally:
        binding.destroy(handle)


def _assert_strict_t1_parity(seed: np.void, out: np.void, ref: np.void, p: int) -> None:
    # Required strict parity lock lanes.
    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["animation_index"][p]) == int(ref["animation_index"][p])
    assert int(out["hitlag"][p]) == int(ref["hitlag"][p])
    assert int(out["hitstun"][p]) == int(ref["hitstun"][p])
    assert np.array_equal(out["state_flags"][p], ref["state_flags"][p])

    # Relevant defender collision-status observable lane.
    assert int(out["hurtbox_state"][p]) == int(ref["hurtbox_state"][p])


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        # Rows repointed 2026-06-12: the original rows (AGNG 271 p0, GAT 209 p1) predate a
        # committed colanim_hit_status_x198c derivation change and no longer carry the
        # chs==1 shine-entry shape under the current pipeline (their parity is still exact).
        # These replacements satisfy the same family preconditions on freshly built debug
        # datasets; verified pre-existing relative to the probe batch (baseline-code run).
        (
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz",
            3145,
            0,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            8262,
            0,
        ),
    ],
)
def test_shine_start_hit_status_entry_family_lock(
    dataset_rel: str, record: int, p: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record)

    # Replay-real lock for Shine Start hit-status ownership:
    # - Fox/Falco Shine Start enter path calls ftAnim_8006EBA4 immediately, so opcode-26 script
    #   hit status is valid on the state-entry frame.
    # - Collision eligibility consumes max(x1988, x198C), so this row family must keep strict t+1
    #   parity on defender-visible lanes.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}
    # refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
    assert int(seed["action_id"][p]) in (360, 365)  # Shine Start ground/air
    assert int(seed["action_frame"][p]) == 1
    assert float(seed["anim_frame_f32"][p]) == pytest.approx(1.0, abs=1e-6)
    assert int(seed["colanim_hit_status_x198c"][p]) == 1
    assert int(seed["hurtbox_state"][p]) == 2

    _assert_strict_t1_parity(seed, out, ref, p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        # Rows repointed 2026-06-12 alongside the family lock above (same derivation-staleness
        # provenance). Under the current colanim lanes the chs==1 af==2 shine rows carry
        # hurtbox_state == 1, so the precondition below pins 1 rather than the old 0.
        (
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz",
            3146,
            0,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            8263,
            0,
        ),
    ],
)
def test_shine_start_hit_status_entry_adjacent_context_controls(
    dataset_rel: str, record: int, p: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record)
    assert int(seed["action_id"][p]) in (360, 365)
    assert int(seed["action_frame"][p]) == 2
    assert float(seed["anim_frame_f32"][p]) == pytest.approx(2.0, abs=1e-6)
    assert int(seed["colanim_hit_status_x198c"][p]) == 1
    assert int(seed["hurtbox_state"][p]) == 1

    _assert_strict_t1_parity(seed, out, ref, p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz",
            183,
            1,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            154,
            0,
        ),
    ],
)
def test_defender_hit_status_max_x1988_x198c_family_lock(
    dataset_rel: str, record: int, p: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record)

    # Replay-real lock for collision eligibility ownership:
    # - defender hit status is computed as max(script x1988, color-animation x198C).
    # - this row family keeps color-animation lane active while the motion-state timeline advances,
    #   and requires strict t+1 parity on action/hitlag/hitstun/state_flags/hurtbox_state lanes.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
    # refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_8006A1BC}
    assert int(seed["colanim_hit_status_x198c"][p]) == 1
    assert int(seed["colanim_timer_x1994"][p]) > 0
    assert int(seed["hurtbox_state"][p]) == 0

    _assert_strict_t1_parity(seed, out, ref, p)


@pytest.mark.integration
@pytest.mark.parametrize(
    ("dataset_rel", "record", "p"),
    [
        (
            "replays/validation/cardinal_1.0_recent/"
            "AttachedGoodNaturedGuanaco.slpz",
            184,
            1,
        ),
        (
            "replays/validation/cardinal_1.0_recent/"
            "GracefulAttachedTurtle.slpz",
            155,
            0,
        ),
    ],
)
def test_defender_hit_status_max_x1988_x198c_adjacent_context_controls(
    dataset_rel: str, record: int, p: int
) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    seed, out, ref = _step_one_row(dataset_path, record)
    assert int(seed["colanim_hit_status_x198c"][p]) == 1
    assert int(seed["colanim_timer_x1994"][p]) > 0
    assert int(seed["hurtbox_state"][p]) == 0

    _assert_strict_t1_parity(seed, out, ref, p)
