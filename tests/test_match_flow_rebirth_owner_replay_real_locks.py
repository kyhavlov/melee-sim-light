from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers, replay_buffer_byte_views


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_one_row(*, dataset_path: Path, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(dataset_path))
    assert int(ds.rows.shape[0]) > record, f"replay too short for record={record}"
    row = ds.rows[record : record + 1]

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, seed_stride
        )
        prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
            1, input_stride
        )
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
        return row["seed_t"][0], row["ref_t1"][0], out
    finally:
        binding.destroy(handle)


def _rollout_to_record(*, dataset_path: Path, start: int, target: int) -> tuple[np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(dataset_path))
    assert int(ds.rows.shape[0]) > target, f"replay too short for record={target}"
    samples = ds.rows
    views = replay_buffer_byte_views(ds)
    seed_u8 = views.seed_t
    prev_input_u8 = views.prev_input_t
    input_u8 = views.input_t

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = seed_u8[start : start + 1, :seed_stride].copy()
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start, target + 1):
            prev_input_bytes = prev_input_u8[record : record + 1, :input_stride].copy()
            input_bytes = input_u8[record : record + 1, :input_stride].copy()
            binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
        return out.copy(), samples["ref_t1"][target].copy()
    finally:
        binding.destroy(handle)


@dataclass(frozen=True)
class _RespawnCase:
    dataset: str
    record: int
    p: int
    source_port0: int
    ref_x: float
    ref_facing: int


_AGG = "replays/validation/aggregate_recent"


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _RespawnCase(f"{_AGG}/HilariousVillainousGiraffe.slpz", 4889, 0, 1, -50.0, 1),
        _RespawnCase(f"{_AGG}/TubbyCurlyHerring.slpz", 4460, 1, 2, 50.0, 0),
    ],
)
def test_rebirth_respawn_uses_replay_source_port_spawn_slot(case: _RespawnCase) -> None:
    # Dead* -> Rebirth reads Player_GetSpawnPlatformPos / Player_GetFacingDirection from the
    # player slot, not from this simulator's compact local player index. Aggregate-suite rows can
    # have local p0/p1 mapped to raw source ports 1/2, so respawn position and facing must follow
    # seed_t.source_port0.
    # Centered respawn points use gm_1601.c::fn_8016719C's `x >= 0.0f` branch and face left.
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D4FF4
    # refs/melee/src/melee/gm/gm_1601.c::fn_8016719C
    # refs/melee/src/melee/pl/player.c::{Player_GetSpawnPlatformPos,Player_GetFacingDirection}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / case.dataset
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset}")

    seed, ref, out = _step_one_row(dataset_path=dataset_path, record=case.record)
    p = case.p
    assert int(seed["source_port0"][p]) == case.source_port0
    assert int(seed["action_id"][p]) in (0, 2, 4)
    assert int(ref["action_id"][p]) == 12
    assert float(ref["pos_x"][p]) == pytest.approx(case.ref_x, abs=1e-4)
    assert int(ref["facing"][p]) == case.ref_facing

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-4)
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-4)
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-4)
    assert int(out["facing"][p]) == int(ref["facing"][p])


@pytest.mark.integration
def test_rebirth_centered_respawn_faces_left_replay_real() -> None:
    # Centered platform-stage respawn points use gm_1601.c::fn_8016719C's `x >= 0.0f` branch,
    # so Rebirth faces left. The same path stores a player-loaded Rebirth start coordinate before
    # Fighter_UnkInitReset_80067C98; on Pokemon Stadium that source start Y is 120, so the first
    # post-transition row lands at y=119 with the x5D0 60-frame timer.
    # refs/melee/src/melee/gm/gm_1601.c::fn_8016719C
    # refs/melee/src/melee/pl/player.c::{Player_80032768,Player_LoadPlayerCoords}
    # refs/melee/src/melee/ft/fighter.c::Fighter_UnkInitReset_80067C98
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset = "replays/validation/pokemon_stadium_recent/CornyDelayedOkapi.slpz"
    dataset_path = root / dataset
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset}")

    seed, ref, out = _step_one_row(dataset_path=dataset_path, record=9226)
    p = 1
    assert int(seed["action_id"][p]) == 0
    assert int(seed["source_port0"][p]) == 1
    assert int(ref["action_id"][p]) == 12
    assert float(ref["pos_x"][p]) == pytest.approx(0.0, abs=1e-4)
    assert float(ref["pos_y"][p]) == pytest.approx(119.0, abs=1e-4)
    assert float(ref["speed_y_self"][p]) == pytest.approx(-1.0, abs=1e-5)
    assert int(ref["facing"][p]) == 0

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-4)
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-5)
    assert int(out["facing"][p]) == int(ref["facing"][p])


@pytest.mark.integration
def test_deadupstar_terminal_source_clear_phase_preserves_owner_one_step() -> None:
    # DeadUpStar can sit on the x18C8 terminal source-owner tick before Rebirth reset. The source
    # clear owner is still Fighter_8006A360, but the replay-facing terminal phase must defer the
    # clear for this DeadUpStar row just like the existing down/passive terminal rows.
    # refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    # refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpStar_Anim
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset = f"{_AGG}/PositiveRevolvingHyena.slpz"
    dataset_path = root / dataset
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset}")

    seed, ref, out = _step_one_row(dataset_path=dataset_path, record=8756)
    p = 0
    assert int(seed["action_id"][p]) == 4
    assert int(seed["source_clear_timer_x18c8"][p]) == 1
    assert int(seed["source_clear_owner_set_phase"][p]) == 1
    assert int(seed["source_clear_terminal_phase"][p]) == 1
    assert int(ref["last_hit_by"][p]) == 1
    assert int(out["last_hit_by"][p]) == int(ref["last_hit_by"][p])


@pytest.mark.integration
def test_rebirthwait_specialairn_exit_applies_x5d8_colanim() -> None:
    # RebirthWait_IASA checks priority aerial specials before the fallback Fall branch, but after a
    # successful exit still calls ftColl_8007B7A4(..., p_ftCommonData->x5D8). That x198C/x1994
    # write is visible as hurtbox_state=1 on the destination SpecialAirNStart row.
    # refs/melee/src/melee/ft/ft_0D4D.c::ftCo_RebirthWait_IASA
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B7A4
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset = f"{_AGG}/TubbyCurlyHerring.slpz"
    dataset_path = root / dataset
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset}")

    seed, ref, out = _step_one_row(dataset_path=dataset_path, record=3062)
    p = 0
    assert int(seed["action_id"][p]) == 13
    assert int(ref["action_id"][p]) == 344
    assert int(ref["hurtbox_state"][p]) == 1

    assert int(out["action_id"][p]) == int(ref["action_id"][p])
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p])
    assert int(out["hurtbox_state"][p]) == int(ref["hurtbox_state"][p])


@pytest.mark.integration
def test_rebirthwait_exit_preserves_tilt_timer_for_fastfall_gate() -> None:
    # RebirthWait -> Fall uses ftCo_Fall_Enter, which passes Ft_MF_KeepFastFall to
    # Fighter_ChangeMotionState and does not reset fp->x671_timer_lstick_tilt_y. A down-held
    # respawn exit must therefore preserve the stale held-down timer and avoid synthesizing a fresh
    # fastfall flick on the first Fall frame.
    # refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_RebirthWait_Anim,ftCo_RebirthWait_IASA}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset = f"{_AGG}/PositiveRevolvingHyena.slpz"
    dataset_path = root / dataset
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset}")

    ds = load_replay_buffers(str(dataset_path))
    seed = ds.rows["seed_t"][8945]
    p = 0
    assert int(seed["action_id"][p]) == 12
    assert int(seed["tilt_timer_y"][p]) == 254

    out, ref = _rollout_to_record(dataset_path=dataset_path, start=8945, target=8990)
    assert int(out["action_id"][p]) == int(ref["action_id"][p]) == 29
    assert int(out["action_frame"][p]) == int(ref["action_frame"][p]) == 1
    assert int(out["state_flags"][p][1]) & 0x08 == 0
    assert int(out["state_flags"][p][1]) == int(ref["state_flags"][p][1])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1e-5)
    assert float(out["speed_y_self"][p]) == pytest.approx(float(ref["speed_y_self"][p]), abs=1e-6)
