from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers

IPW = "replays/validation/marth/InternalPowerlessWallaby.slpz"
VSA = "replays/validation/marth/VictoriousSpitefulAlpaca.slpz"

ACT_JUMP_AERIAL_F = 0x001B
ACT_CLIFF_CATCH = 0x00FC
ACT_MS_SPECIAL_AIR_HI = 368


def _one_step(dataset_rel: str, record: int) -> tuple[np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    root = Path(__file__).resolve().parents[1]
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")
    ds = load_replay_buffers(str(dataset_path))
    samples = ds.rows
    sizes = binding.sizes()

    def _field_bytes(field: str, stride: int) -> np.ndarray:
        row = samples[record : record + 1]
        return np.frombuffer(row[field].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, stride)

    handle = binding.init(
        batch_size=1,
        num_players=int(ds.num_players),
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )
    try:
        binding.reseed_seed(handle, _field_bytes("seed_t", int(sizes["seed"])))
        binding.step_input(
            handle,
            _field_bytes("prev_input_t", int(sizes["input"])),
            _field_bytes("input_t", int(sizes["input"])),
        )
        out_bytes = np.empty((1, int(sizes["compare"])), dtype=np.uint8)
        binding.write_compare(handle, out_bytes)
        out = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        return samples[record]["seed_t"].copy(), out, samples[record]["ref_t1"].copy()
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_jumpaerial_apex_ledge_catch_uses_centered_ecb_bottom() -> None:
    # cd->ecb snaps to desired_ecb (mpCollInterpolateECB time=1.0 on the final substep), and
    # desired bottom.x is centered under cur_pos. Sampling the instantaneous POSED bottom-X
    # swung marth's JumpAerial flip bottom past the right-ledge side gate
    # (cur+bottom.x 62.93 < edge 63.35) and dropped the catch on the apex frame. IPW rec
    # 5178 p0: marth double-jump apex at the FoD right ledge must CliffCatch this step
    # (the JumpAerialF->CliffCatch rollout cluster, impact 1354 freq 22).
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_800443C4}
    seed, out, ref = _one_step(IPW, 5178)
    assert int(seed["action_id"][0]) == ACT_JUMP_AERIAL_F
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_CLIFF_CATCH


@pytest.mark.integration
@pytest.mark.parametrize("record", [1860, 2205])
def test_ds_under_lip_ledge_grab_still_fires_without_posed_bottom_x(record: int) -> None:
    # Regression control for retiring the posed bottom-X table: the table was originally
    # introduced for THIS family (Dolphin Slash under-lip sweetspot grabs, marth_port
    # TRIAGE_MARTH_ROWS.md item 2 - "the sim used raw cur_pos.x ... so under-lip DS grabs
    # never fired"). Under the snapped/centered cd->ecb bottom model these grabs must keep
    # firing on time: VSA recs 1860/2205 p0, air Dolphin Slash (368) af 22 -> CliffCatch af 1.
    # refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044164,mpColl_800443C4}
    # refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::ftMs_SpecialAirHi_Coll
    seed, out, ref = _one_step(VSA, record)
    assert int(seed["action_id"][0]) == ACT_MS_SPECIAL_AIR_HI
    assert int(seed["action_frame"][0]) == 22
    assert int(out["action_id"][0]) == int(ref["action_id"][0]) == ACT_CLIFF_CATCH
    assert int(out["action_frame"][0]) == int(ref["action_frame"][0]) == 1
