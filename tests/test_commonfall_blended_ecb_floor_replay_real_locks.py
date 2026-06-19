from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tests.test_colldata_ecb_substrate import _colldata_ecb_dtype
from tests.test_combat_ownership_seed_guardrail_locks import _skip_if_required_artifacts_missing
from tools.eval.dataset import COMPARE_DTYPE, read_dataset


ACT_FALL = 29
ACT_FALL_SPECIAL = 35
ACT_LANDING = 42
ACT_LANDING_FALL_SPECIAL = 43
FLOOR_MODE_BOTTOM_SWEEP = 1


def _run_rollout_records(ds, *, start_record: int, records: tuple[int, ...]) -> dict[int, tuple[np.void, np.void]]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    colldata_stride = int(sizes["colldata_ecb"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    wanted = set(records)
    max_record = max(wanted)
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)
    colldata_bytes = np.empty((1, colldata_stride), dtype=np.uint8)
    out: dict[int, tuple[np.void, np.void]] = {}

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.header["num_players"]),
        ucf_enabled=True,
        ucf_cardinals_1_0_enabled=True,
    )
    try:
        seed_bytes = ds.samples[start_record : start_record + 1]["seed_t"].view("u1").reshape(
            1, seed_stride
        ).copy()
        binding.reseed_seed_rollout(handle, seed_bytes)
        for record in range(start_record, max_record + 1):
            row = ds.samples[record : record + 1]
            binding.step_input(
                handle,
                row["prev_input_t"].view("u1").reshape(1, input_stride).copy(),
                row["input_t"].view("u1").reshape(1, input_stride).copy(),
            )
            if record in wanted:
                binding.write_compare(handle, out_bytes)
                binding.debug_write_colldata_ecb(handle, colldata_bytes)
                out[record] = (
                    out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy(),
                    colldata_bytes.view(_colldata_ecb_dtype()).reshape((1,))[0].copy(),
                )
    finally:
        binding.destroy(handle)

    return out


@pytest.mark.integration
def test_sheik_demo_commonfall_blended_ecb_lands_on_floor_sweep_rec191() -> None:
    # Replay-real lock for official Sheik demo rec191:
    # - ftCo_Fall_Anim_Inner has selected/blended the FallB JObj pose in mv.co.fall.x4.
    # - Fall_Coll then routes through ft_800831CC -> mpColl_80047E14(flags=6).
    # - mpCollInterpolateECB consumes that live blended CollData ECB before mpColl_80044628_Floor,
    #   so the bottom sweep reaches FD's hard floor on rec191. The previous rec190 frame is the
    #   adjacent negative: the same owner still keeps Sheik airborne because the blended ECB bottom
    #   remains above the floor.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
    #   ftCo_Fall_Anim_Inner,ftCo_Fall_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80047E14,mpColl_80044628_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/sheik/replays/validation/sheik/sheik_demo_game.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    p = 0
    assert int(ds.samples[190]["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(ds.samples[190]["ref_t1"]["action_id"][p]) == ACT_FALL
    assert int(ds.samples[191]["seed_t"]["action_id"][p]) == ACT_FALL
    assert int(ds.samples[191]["seed_t"]["common_fall_blend_valid_u8"][p]) == 1
    assert float(ds.samples[191]["seed_t"]["common_fall_blend_x4_f32"][p]) > 0.0
    assert int(ds.samples[191]["ref_t1"]["action_id"][p]) == ACT_LANDING
    assert int(ds.samples[191]["ref_t1"]["ground_id"][p]) == 1

    rows = _run_rollout_records(ds, start_record=139, records=(190, 191))

    out_190, colldata_190 = rows[190]
    assert int(out_190["action_id"][p]) == ACT_FALL
    assert int(out_190["on_ground"][p]) == 0
    assert int(colldata_190["floor_result_valid"][p]) == 0
    assert float(colldata_190["floor_probe_cur_bottom_y"][p]) > 0.0

    out_191, colldata_191 = rows[191]
    ref_191 = ds.samples[191]["ref_t1"]
    assert int(out_191["action_id"][p]) == ACT_LANDING
    assert int(out_191["on_ground"][p]) == 1
    assert int(out_191["ground_id"][p]) == int(ref_191["ground_id"][p])
    assert float(out_191["pos_y"][p]) == pytest.approx(float(ref_191["pos_y"][p]), abs=1.0e-6)
    assert int(colldata_191["floor_result_valid"][p]) == 1
    assert int(colldata_191["floor_result_mode"][p]) == FLOOR_MODE_BOTTOM_SWEEP
    assert int(colldata_191["floor_result_segment_id"][p]) == 1
    assert float(colldata_191["floor_probe_cur_bottom_y"][p]) < 0.0


@pytest.mark.integration
def test_sheik_demo_commonfall_static_platform_bottom_interval_lands_rec3387_6413() -> None:
    # Replay-real locks for the official Sheik demo Battlefield platform floor sweeps:
    # - rec3387 p0: Sheik Fall carries a tiny CommonFall blend, and the live callback-local ECB
    #   bottom crosses the right platform from above to below while the replay-visible root is
    #   already below the one-way platform.
    # - rec6413 p1: Marth Fall has no nonzero blend, but the ordinary Fall ECB bottom has the same
    #   above->below one-way-platform interval.
    # In both cases source `mpColl_80044628_Floor` owns the bottom interval acceptance before
    # `mpColl_80044838_Floor`; the stale same-platform "root below platform" guard must not reject
    # a real callback-local bottom sweep. The previous rows are adjacent negatives whose current
    # bottom remains above the platform.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
    #   ftCo_Fall_Anim_Inner,ftCo_Fall_Coll}
    # refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
    # refs/melee/src/melee/mp/mpcoll.c::{
    #   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80047E14,mpColl_80044628_Floor,
    #   mpColl_80044838_Floor}
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/sheik/replays/validation/sheik/sheik_demo_game.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    targets = ((3386, 3387, 0, 4), (6412, 6413, 1, 2))
    for before_rec, land_rec, player, ground_id in targets:
        assert int(ds.samples[before_rec]["seed_t"]["action_id"][player]) == ACT_FALL
        assert int(ds.samples[before_rec]["ref_t1"]["action_id"][player]) == ACT_FALL
        assert int(ds.samples[land_rec]["seed_t"]["action_id"][player]) == ACT_FALL
        assert int(ds.samples[land_rec]["ref_t1"]["action_id"][player]) == ACT_LANDING
        assert int(ds.samples[land_rec]["ref_t1"]["ground_id"][player]) == ground_id

        rows = _run_rollout_records(ds, start_record=before_rec, records=(before_rec, land_rec))

        out_before, colldata_before = rows[before_rec]
        assert int(out_before["action_id"][player]) == ACT_FALL
        assert int(out_before["on_ground"][player]) == 0
        assert int(colldata_before["floor_result_valid"][player]) == 0
        assert float(colldata_before["floor_probe_cur_bottom_y"][player]) > 27.2000

        out_land, colldata_land = rows[land_rec]
        ref_land = ds.samples[land_rec]["ref_t1"]
        assert int(out_land["action_id"][player]) == ACT_LANDING
        assert int(out_land["on_ground"][player]) == 1
        assert int(out_land["ground_id"][player]) == int(ref_land["ground_id"][player])
        assert float(out_land["pos_y"][player]) == pytest.approx(
            float(ref_land["pos_y"][player]), abs=1.0e-6
        )
        assert int(colldata_land["floor_result_valid"][player]) == 1
        assert int(colldata_land["floor_result_mode"][player]) == FLOOR_MODE_BOTTOM_SWEEP
        assert int(colldata_land["floor_result_segment_id"][player]) == ground_id
        assert float(colldata_land["floor_probe_prev_bottom_y"][player]) > float(
            ref_land["pos_y"][player]
        )
        assert float(colldata_land["floor_probe_cur_bottom_y"][player]) < float(
            ref_land["pos_y"][player]
        )


@pytest.mark.integration
def test_fox_fallspecial_rollout_does_not_borrow_commonfall_blended_ecb_tch_11574() -> None:
    # Replay-real aggregate lock for TCH rec11574:
    # - Fox/Falco character attrs have `common_fall_blended_ecb_seed_mask == 0`, so a long
    #   free-running rollout must not apply the Sheik/Marth CommonFall blended-ECB owner to
    #   Fox FallSpecial/FallSpecialF/B.
    # - A broad live x4 consumer keeps Fox in FallSpecial here; source lands into
    #   LandingFallSpecial on the hard floor.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
    # data/characters/{fox,falco,sheik,marth}.json::common_fall_blended_ecb_seed_mask
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/TubbyCurlyHerring.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    p = 0
    assert int(ds.samples[11574]["seed_t"]["action_id"][p]) == ACT_FALL_SPECIAL
    assert int(ds.samples[11574]["ref_t1"]["action_id"][p]) == ACT_LANDING_FALL_SPECIAL

    fox = json.loads((root / "data/characters/fox.json").read_text())
    falco = json.loads((root / "data/characters/falco.json").read_text())
    sheik = json.loads((root / "data/characters/sheik.json").read_text())
    marth = json.loads((root / "data/characters/marth.json").read_text())
    assert int(fox["common_fall_blended_ecb_seed_mask"]) == 0
    assert int(falco["common_fall_blended_ecb_seed_mask"]) == 0
    assert int(sheik["common_fall_blended_ecb_seed_mask"]) == 1
    assert int(marth["common_fall_blended_ecb_seed_mask"]) == 2

    rows = _run_rollout_records(ds, start_record=11570, records=(11574,))
    out, _colldata = rows[11574]
    ref = ds.samples[11574]["ref_t1"]
    assert int(out["action_id"][p]) == ACT_LANDING_FALL_SPECIAL
    assert int(out["on_ground"][p]) == 1
    assert int(out["ground_id"][p]) == int(ref["ground_id"][p])
    assert float(out["pos_y"][p]) == pytest.approx(float(ref["pos_y"][p]), abs=1.0e-6)
