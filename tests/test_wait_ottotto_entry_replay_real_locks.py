from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

import pytest

from tests.test_combat_ownership_seed_guardrail_locks import _run_one_step_row, _skip_if_required_artifacts_missing
from tools.eval.dataset import read_dataset


@dataclass(frozen=True)
class _WaitOttottoCase:
    record: int
    seed_action_id: int
    ref_action_id: int
    ref_action_frame: int
    note: str


@pytest.mark.integration
@pytest.mark.parametrize(
    "case",
    [
        _WaitOttottoCase(
            record=3894,
            seed_action_id=20,
            ref_action_id=14,
            ref_action_frame=0,
            note="QGD Wait->Ottotto family target-1 row",
        ),
        _WaitOttottoCase(
            record=3895,
            seed_action_id=14,
            ref_action_id=245,
            ref_action_frame=0,
            note="QGD Wait->Ottotto family target row",
        ),
        _WaitOttottoCase(
            record=3896,
            seed_action_id=245,
            ref_action_id=245,
            ref_action_frame=1,
            note="QGD Wait->Ottotto family target+1 row",
        ),
    ],
)
def test_wait_edge_loss_enters_ottotto_qgd_replay_real_lock(case: _WaitOttottoCase) -> None:
    # Replay-real lock for steady Wait walk-off -> Ottotto:
    # - ftCo_8009A3C8 checks Collide_Edge and routes into ftCo_8009A410 (Ottotto entry).
    # - This lock targets the steady Wait family where the collision callback, not a same-frame
    #   intermediate Wait carry, owns the edge-loss transition.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_8009A3C8,ftCo_8009A410}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "datasets/fox_falco_fd_ucf084_recent/replays/debug/"
        "cardinal_1.0_recent/QuerulousGrandDinosaur.msl"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    record = int(case.record)
    p = 1
    assert int(samples.shape[0]) > record, f"dataset too short for lock row: record={record}"

    row = samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]
    assert int(seed["action_id"][p]) == int(case.seed_action_id), case.note
    assert int(ref["action_id"][p]) == int(case.ref_action_id), case.note
    assert int(ref["action_frame"][p]) == int(case.ref_action_frame), case.note
    assert int(seed["hitlag"][p]) == 0, case.note
    assert int(ref["hitlag"][p]) == 0, case.note
    assert int(seed["hitstun"][p]) == 0, case.note
    assert int(ref["hitstun"][p]) == 0, case.note

    _, ref_row, out_row = _run_one_step_row(dataset_path, record, p)
    for field in ("action_id", "action_frame", "animation_index", "on_ground", "ground_id"):
        got = int(out_row[field][p])
        want = int(ref_row[field][p])
        assert got == want, f"record={record} p={p} field={field} expected={want} got={got}"
