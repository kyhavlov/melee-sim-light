from __future__ import annotations

from collections.abc import Callable
from functools import lru_cache
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE
from tools.slippi.make_dataset_from_slp import build_dataset_from_slp

HIT_GROUNDED = 1 << 9
HIT_AERIAL = 1 << 10


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/common/ft_common_data.json",
        "data/characters/sheik.json",
        "data/characters/marth.json",
        "data/anims/sheik.tracks.bin",
        "data/anims/marth.tracks.bin",
        "data/moves/sheik.json",
        "data/moves/marth.json",
        "replays/validation/sheik/TenseSameHummingbird.slpz",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local artifacts: {', '.join(missing)}")


@lru_cache(maxsize=1)
def _tense_throwhi_row():
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    ds = build_dataset_from_slp(
        slp_path=str(root / "replays/validation/sheik/TenseSameHummingbird.slpz"),
        ports=[1, 3],
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    return ds, ds.samples[6883:6884]


def _seed_input_bytes(row, sizes, seed_mutator: Callable[[np.ndarray], None] | None = None):
    seed = row["seed_t"].copy()
    if seed_mutator is not None:
        seed_mutator(seed)
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    seed_bytes = np.frombuffer(seed.tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    return seed_bytes, prev_input_bytes, input_bytes


def _force_far_authored_throw_hitbox(binding, handle) -> None:
    binding.debug_set_hitbox_world(handle, 0, 0, 0, 1000.0, 1000.0, 0.0, 5.0778, 6.0, 1)
    binding.debug_set_hitbox_flags(handle, 0, 0, 0, HIT_GROUNDED | HIT_AERIAL)
    binding.debug_set_hitbox_group(handle, 0, 0, 0, 0)
    binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 361, 0, 0, 0)
    for hb in (1, 2, 3):
        binding.debug_set_hitbox_world(handle, 0, 0, hb, 1000.0, 1000.0, 0.0, 0.0, 0.0, 0)


def _selected_contacts(
    *,
    seed_mutator: Callable[[np.ndarray], None] | None = None,
    debug_mutator: Callable[[object, object], None] | None = None,
) -> np.ndarray:
    ds, row = _tense_throwhi_row()
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_bytes, prev_input_bytes, input_bytes = _seed_input_bytes(row, sizes, seed_mutator)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        if debug_mutator is not None:
            debug_mutator(binding, handle)
        selected_raw, selected_count = binding.debug_combat_select_body_hits(handle, 0, 64)
    finally:
        binding.destroy(handle)
    return selected_raw[: int(selected_count)].copy()


@pytest.mark.integration
def test_attached_throwhi_body_contact_survives_source_correct_fobj_eof_tsh_6883() -> None:
    # TenseSameHummingbird:6883 locks the source-correct FObj EOF exposure for Sheik ThrowHi:
    # the thrower's pre-release BODY HitCapsule hits an attached ThrownHi victim and applies
    # thrower hitlag plus attached-victim percent/bookkeeping without releasing the victim.
    ds, row = _tense_throwhi_row()
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][0]) == 221  # ThrowHi
    assert int(seed["action_id"][1]) == 241  # ThrownHi
    assert int(seed["action_frame"][0]) == 18
    assert int(seed["grab_owner_port"][1]) == 0

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    compare_stride = int(sizes["compare"])
    seed_bytes, prev_input_bytes, input_bytes = _seed_input_bytes(row, sizes)

    selected = _selected_contacts()
    assert len(selected) >= 1
    assert [int(x) for x in selected[0, :5]] == [0, 1, 0, 0, 249]

    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)
    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]

    assert int(out["hitlag"][0]) == int(ref["hitlag"][0]) == 5
    assert int(out["hitlag"][1]) == int(ref["hitlag"][1]) == 0
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]), abs=1e-6)
    assert int(out["instance_hit_by"][1]) == int(ref["instance_hit_by"][1])
    assert int(out["last_attack_landed"][0]) == int(ref["last_attack_landed"][0])
    np.testing.assert_array_equal(out["state_flags"][:2], ref["state_flags"][:2])


def test_attached_throw_body_pose_gap_admits_authored_no_overlap_positive() -> None:
    selected = _selected_contacts(debug_mutator=_force_far_authored_throw_hitbox)
    assert len(selected) >= 1
    assert [int(x) for x in selected[0, :5]] == [0, 1, 0, 0, 249]


@pytest.mark.parametrize("wrong_owner", [2, 255])
def test_attached_throw_body_pose_gap_rejects_wrong_grab_owner(wrong_owner: int) -> None:
    def mutate(binding, handle) -> None:
        _force_far_authored_throw_hitbox(binding, handle)
        binding.debug_set_grab_owner_port(handle, 0, 1, wrong_owner)

    assert len(_selected_contacts(debug_mutator=mutate)) == 0


def test_attached_throw_body_pose_gap_rejects_unattached_victim() -> None:
    def mutate(binding, handle) -> None:
        _force_far_authored_throw_hitbox(binding, handle)
        binding.debug_set_damage_phase(handle, 0, 1, 14, 0, 0, 0)
        binding.debug_set_grab_owner_port(handle, 0, 1, 255)

    assert len(_selected_contacts(debug_mutator=mutate)) == 0


def test_attached_throw_body_pose_gap_rejects_post_release_frame() -> None:
    def seed_post_release(seed: np.ndarray) -> None:
        seed["action_frame"][0, 0] = 23
        seed["anim_frame_f32"][0, 0] = 23.0

    assert len(_selected_contacts(seed_mutator=seed_post_release, debug_mutator=_force_far_authored_throw_hitbox)) == 0


def test_attached_throw_body_pose_gap_rejects_nonzero_direct_kb() -> None:
    def mutate(binding, handle) -> None:
        _force_far_authored_throw_hitbox(binding, handle)
        binding.debug_set_hitbox_kb_params(handle, 0, 0, 0, 361, 100, 0, 10)

    assert len(_selected_contacts(debug_mutator=mutate)) == 0


def test_attached_throw_body_pose_gap_rejects_non_throw_zero_kb_body_hitbox() -> None:
    def seed_non_throw(seed: np.ndarray) -> None:
        seed["action_id"][0, 0] = 14
        seed["animation_index"][0, 0] = 14

    assert len(_selected_contacts(seed_mutator=seed_non_throw, debug_mutator=_force_far_authored_throw_hitbox)) == 0
