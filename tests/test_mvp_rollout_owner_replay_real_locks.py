from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_GUARD = 0x00B3
ACT_GUARD_SET_OFF = 0x00B5
ACT_DOWN_BOUND_U = 0x00B7
ACT_DOWN_STAND_U = 0x00BA
ACT_ESCAPE_AIR = 0x00EC
ACT_LANDING_FALL_SPECIAL = 0x002B


def _dataset_path(root: Path) -> Path:
    return root / "datasets/aggregate_recent/replays/validation/battlefield_recent/MediumVirtualPig.msl"


def _skip_if_dataset_missing(root: Path, dataset_path: Path | None = None) -> None:
    if dataset_path is None:
        dataset_path = _dataset_path(root)
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_path}")


def _bytes(row: np.ndarray, field: str, stride: int) -> np.ndarray:
    return np.frombuffer(row[field].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)


def _step_replay_row(binding, handle, samples: np.ndarray, record: int, sizes) -> None:
    row = samples[record : record + 1]
    binding.step_input_replay_frame_rng(
        handle,
        _bytes(row, "seed_t", int(sizes["seed"])),
        _bytes(row, "prev_input_t", int(sizes["input"])),
        _bytes(row, "input_t", int(sizes["input"])),
    )


def _run_rollout_to_records(ds, start_record: int, records: tuple[int, ...]) -> dict[int, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    compare_stride = int(sizes["compare"])
    samples = ds.samples
    assert max(records) >= start_record

    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    out_view = out_bytes.view(COMPARE_DTYPE).reshape(1)
    out: dict[int, np.void] = {}

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(
            handle, _bytes(samples[start_record : start_record + 1], "seed_t", int(sizes["seed"]))
        )
        for record in range(start_record, max(records) + 1):
            _step_replay_row(binding, handle, samples, record, sizes)
            if record in records:
                binding.write_compare(handle, out_bytes)
                out[record] = out_view[0].copy()
    finally:
        binding.destroy(handle)

    return out


def _run_one_replay_row(ds, record: int) -> np.void:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    compare_stride = int(sizes["compare"])
    samples = ds.samples
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed_rollout(
            handle, _bytes(samples[record : record + 1], "seed_t", int(sizes["seed"]))
        )
        _step_replay_row(binding, handle, samples, record, sizes)
        binding.write_compare(handle, out_bytes)
    finally:
        binding.destroy(handle)

    return out_bytes.view(COMPARE_DTYPE).reshape(1)[0].copy()


def _assert_discrete_matches(out: np.void, ref: np.void, *, players: int, note: str) -> None:
    for field in ("action_id", "animation_index", "on_ground", "ground_id", "hitlag", "hitstun"):
        assert [int(x) for x in out[field][:players]] == [int(x) for x in ref[field][:players]], note


@pytest.mark.integration
def test_mvp_escapeair_and_downed_callback_rows_stay_rollout_clean() -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_dataset_missing(root)
    ds = read_dataset(str(_dataset_path(root)))
    samples = ds.samples
    players = int(ds.header["num_players"])

    # MVP:3104 is the hard-floor EscapeAir continuation. The live JumpAerial desired-bottom lane
    # must not be directly published as a landing unless the current EscapeAir callback owns a real
    # floor producer.
    escapeair = _run_rollout_to_records(ds, 2981, (3104,))[3104]
    ref_3104 = samples["ref_t1"][3104]
    assert int(samples["seed_t"][3104]["action_id"][1]) == ACT_ESCAPE_AIR
    assert int(ref_3104["action_id"][1]) == ACT_ESCAPE_AIR
    assert int(ref_3104["action_id"][1]) != ACT_LANDING_FALL_SPECIAL
    _assert_discrete_matches(escapeair, ref_3104, players=players, note="MVP rec3104 EscapeAir")

    # MVP:787 is the adjacent platform negative. It has the same EscapeAir/JumpAerial frame-2
    # desired-bottom lineage, but source owns the platform landing; the hard-floor suppressor must
    # not erase it.
    platform = _run_rollout_to_records(ds, 664, (787,))[787]
    ref_787 = samples["ref_t1"][787]
    assert int(samples["seed_t"][787]["action_id"][1]) == ACT_ESCAPE_AIR
    assert int(ref_787["action_id"][1]) == ACT_LANDING_FALL_SPECIAL
    assert int(ref_787["on_ground"][1]) == 1
    _assert_discrete_matches(platform, ref_787, players=players, note="MVP rec787 platform")

    # MVP:7600 enters DownBoundU from the DamageFly collision callback. MVP:7626 then enters
    # DownStandU from the downed callback path. Both are same-callback motion entries whose source
    # paths do not immediately run ftAnim_8006EBA4, so frame-0 hitbox scripts must not fabricate a
    # shield hit on the guarding opponent.
    downed = _run_rollout_to_records(ds, 7503, (7600, 7626))
    ref_7600 = samples["ref_t1"][7600]
    ref_7626 = samples["ref_t1"][7626]
    assert int(ref_7600["action_id"][0]) == ACT_DOWN_BOUND_U
    assert int(ref_7626["action_id"][0]) == ACT_DOWN_STAND_U
    assert int(ref_7626["action_id"][1]) == ACT_GUARD
    assert int(ref_7626["action_id"][1]) != ACT_GUARD_SET_OFF
    _assert_discrete_matches(downed[7600], ref_7600, players=players, note="MVP rec7600 DownBound")
    _assert_discrete_matches(downed[7626], ref_7626, players=players, note="MVP rec7626 DownStand")

    damageflyroll = _run_rollout_to_records(ds, 1718, (4156,))[4156]
    ref_4156 = samples["ref_t1"][4156]
    assert int(samples["seed_t"][4156]["action_id"][0]) == 0x0164  # SpecialAirHi
    assert int(ref_4156["action_id"][0]) == 0x005B  # DamageFlyRoll
    _assert_discrete_matches(
        damageflyroll, ref_4156, players=players, note="MVP rec4156 DamageFlyRoll rollout"
    )


@pytest.mark.integration
@pytest.mark.parametrize(
    "dataset_rel,start_record,record,player,note",
    [
        (
            "datasets/aggregate_recent/replays/validation/battlefield_recent/LoyalDishonestWren.msl",
            44,
            167,
            1,
            "LDW EscapeAir hard-floor landing control",
        ),
        (
            "datasets/aggregate_recent/replays/validation/pokemon_stadium_recent/SweatyThisMallard.msl",
            992,
            1115,
            1,
            "STM EscapeAir hard-floor landing control",
        ),
    ],
)
def test_escapeair_jumpaerial_hard_floor_owner_does_not_suppress_controls(
    dataset_rel: str, start_record: int, record: int, player: int, note: str
) -> None:
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    _skip_if_dataset_missing(root, dataset_path)
    ds = read_dataset(str(dataset_path))
    out = _run_rollout_to_records(ds, start_record, (record,))[record]
    ref = ds.samples["ref_t1"][record]
    assert int(ds.samples["seed_t"][record]["action_id"][player]) == ACT_ESCAPE_AIR
    assert int(ref["action_id"][player]) == ACT_LANDING_FALL_SPECIAL
    assert int(ref["on_ground"][player]) == 1
    _assert_discrete_matches(out, ref, players=int(ds.header["num_players"]), note=note)


@pytest.mark.integration
@pytest.mark.parametrize(
    "record,note",
    [
        (973, "AttackAirLw strong DAir tail-only BODY reject positive"),
        (974, "AttackAirLw strong DAir adjacent full-hit control"),
        (2598, "GuardSetOff turnover player-nudge owner"),
        (2933, "ftCo_800D0EC8 landing threshold argument order"),
        (6500, "AttackAirLw weak multihit shield extent reject"),
        (7595, "received phantom/tip-log hitlag priority over outgoing deal-hitlag"),
    ],
)
def test_mvp_one_step_owner_proving_rows_match(record: int, note: str) -> None:
    root = Path(__file__).resolve().parents[1]
    _skip_if_dataset_missing(root)
    ds = read_dataset(str(_dataset_path(root)))
    out = _run_one_replay_row(ds, record)
    ref = ds.samples["ref_t1"][record]
    _assert_discrete_matches(out, ref, players=int(ds.header["num_players"]), note=note)
