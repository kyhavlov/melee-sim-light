from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/moves/fox.json",
        "data/moves/falco.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _step_one_row(*, binding, row: np.ndarray, num_players: int) -> np.void:
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
      seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
          1, seed_stride
      )
      prev_input_bytes = (
          np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
              1, input_stride
          )
      )
      input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
          1, input_stride
      )
      out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

      binding.reseed_seed(handle, seed_bytes)
      binding.step_input(handle, prev_input_bytes, input_bytes)
      binding.write_compare(handle, out_compare_bytes)
      return out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    finally:
      binding.destroy(handle)


@dataclass(frozen=True)
class _Case:
    dataset_rel: str
    record: int
    victim_p: int
    expected_facing: int
    note: str


_CASES = (
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
        record=3426,
        victim_p=0,
        expected_facing=1,
        note="ThrowHi nearby control before state1 hitlag extension stays matched",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
        record=3427,
        victim_p=0,
        expected_facing=1,
        note="ThrowHi state1 shot keeps victim facing from thrower-facing lane on ongoing DamageAir2",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/GracefulAttachedTurtle.slpz",
        record=3428,
        victim_p=0,
        expected_facing=0,
        note="ThrowHi negative control: fully-saturated hitlag row must keep generic facing match",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
        record=5087,
        victim_p=0,
        expected_facing=0,
        note="ThrowHi nearby control before DamageFlyTop entry stays matched",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
        record=5088,
        victim_p=0,
        expected_facing=0,
        note="ThrowHi nearby control on DamageFlyTop entry stays matched",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
        record=5090,
        victim_p=0,
        expected_facing=0,
        note="ThrowHi state1 shot keeps victim facing from thrower-facing lane on ongoing DamageAir2",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
        record=5091,
        victim_p=0,
        expected_facing=0,
        note="ThrowHi adjacent ongoing DamageAir2 row stays matched",
    ),
    _Case(
        dataset_rel="replays/validation/cardinal_1.0_recent/TreasuredBackKangaroo.slpz",
        record=5092,
        victim_p=0,
        expected_facing=1,
        note="ThrowHi negative control: fully-saturated hitlag row must keep generic facing match",
    ),
)


@pytest.mark.integration
@pytest.mark.parametrize("case", _CASES, ids=lambda c: f"{Path(c.dataset_rel).stem}:rec{c.record}:p{c.victim_p}")
def test_throwhi_item_damage_facing_replay_real_locks(case: _Case) -> None:
    # Replay-real lock for ThrowHi throw-side blaster hits:
    # - ftFx_Throw_Anim spawns the state1 shot via it_8029C6CC while ThrowHi is still active.
    # - The resulting damage-facing ownership must match the thrower-facing lane on these replay-real
    #   rows instead of inheriting the generic owner-root source transform.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_path = root / case.dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {case.dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[case.record : case.record + 1]
    p = int(case.victim_p)
    thrower = 1 - p

    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    assert int(seed["action_id"][thrower]) == 221, case.note  # ThrowHi
    if case.record in (3426, 3427, 3428, 5087, 5088, 5090, 5091, 5092):
        assert int(seed["items"][1]["type"]) == 55, case.note
        assert int(seed["items"][1]["state"]) == 1, case.note
        assert int(seed["items"][1]["owner"]) == thrower, case.note
    assert int(ref["facing"][p]) == int(case.expected_facing), case.note

    binding = pytest.importorskip("msl_binding")
    out = _step_one_row(binding=binding, row=row, num_players=int(ds.num_players))

    for field in ("action_id", "action_frame", "facing", "animation_index"):
        assert int(out[field][p]) == int(ref[field][p]), (
            f"{case.note}: field={field} expected={int(ref[field][p])} got={int(out[field][p])}"
        )
