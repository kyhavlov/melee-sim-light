from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


QGD = (
    "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
    "QuerulousGrandDinosaur.msl"
)
BHH = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "BlondHardHippopotamus.msl"
)
TCH = (
    "datasets/aggregate_recent/replays/validation/aggregate_recent/"
    "TubbyCurlyHerring.msl"
)
GAT = (
    "datasets/aggregate_recent/replays/validation/cardinal_1.0_recent/"
    "GracefulAttachedTurtle.msl"
)


def _run_row(
    record: int, dataset_rel: str = QGD
) -> tuple[np.void, np.void, np.void, np.void]:
    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    root = Path(__file__).resolve().parents[1]
    ds = read_dataset(str(root / dataset_rel))
    row = ds.samples[record : record + 1]

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    contact_dtype = np.dtype(
        [
            ("wall_kind", ("u1", (4,))),
            ("_pad0", ("u1", (4,))),
            ("wall_id", ("<u2", (4,))),
            ("wall_contact_x", ("<f4", (4,))),
            ("wall_contact_y", ("<f4", (4,))),
            ("wall_normal_x", ("<f4", (4,))),
            ("wall_normal_y", ("<f4", (4,))),
            ("ceiling_id", ("<u2", (4,))),
            ("_pad1", ("<u2", (4,))),
            ("ceiling_contact_x", ("<f4", (4,))),
            ("ceiling_contact_y", ("<f4", (4,))),
            ("ceiling_normal_x", ("<f4", (4,))),
            ("ceiling_normal_y", ("<f4", (4,))),
            ("coll_env_flags", ("<u4", (4,))),
            ("coll_prev_env_flags", ("<u4", (4,))),
        ],
        align=False,
    )
    contact_bytes = np.zeros((1, contact_dtype.itemsize), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_bytes)
        binding.debug_write_collision_contacts(handle, contact_bytes)
    finally:
        binding.destroy(handle)

    got = out_bytes.view(COMPARE_DTYPE).reshape(-1)[0].copy()
    contacts = contact_bytes.view(contact_dtype).reshape(-1)[0].copy()
    return row["seed_t"][0], row["ref_t1"][0], got, contacts


@pytest.mark.integration
@pytest.mark.parametrize(
    ("record", "wall_id"),
    [
        (9372, 10),
        (9373, 10),
        (9374, 9),
    ],
)
def test_specialairhi_right_wall_jobj_ecb_envelope_matches_qgd_positive(
    record: int, wall_id: int
) -> None:
    # Decomp owner:
    # - SpecialAirHi_Coll -> ft_CheckGroundAndLedge -> mpColl_800473CC.
    # - mpColl_LoadECB_JObj samples the live FtPart_XRotN-rotated collision joints.
    # - mpColl_800454A4_RightWall resolves the airborne right-wall envelope after the sweep.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
    # refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_800454A4_RightWall}
    _, ref, got, contacts = _run_row(record)

    assert int(got["action_id"][0]) == int(ref["action_id"][0]) == 356
    assert int(got["action_id"][1]) == int(ref["action_id"][1])
    assert int(got["hitlag"][1]) == int(ref["hitlag"][1])
    assert int(contacts["wall_kind"][0]) == 2
    assert int(contacts["wall_id"][0]) == wall_id
    assert float(got["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-5)
    assert float(got["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)


@pytest.mark.integration
def test_specialairhi_jobj_ecb_does_not_create_precontact_wall_on_qgd_control() -> None:
    _, ref, got, contacts = _run_row(9371)

    assert int(got["action_id"][0]) == int(ref["action_id"][0]) == 356
    assert int(contacts["coll_env_flags"][0]) == 0
    assert int(contacts["wall_kind"][0]) == 0
    np.testing.assert_allclose(got["pos_x"], ref["pos_x"], atol=1e-6)
    np.testing.assert_allclose(got["pos_y"], ref["pos_y"], atol=1e-6)


@pytest.mark.integration
def test_specialairhi_horizontal_right_wall_keeps_centered_ecb_control() -> None:
    # Horizontal Firefox/Firebird has an identity XRotN launch angle; this stays on the root-Y
    # facing basis and mpColl_LoadECB_JObj x12C recentering rather than the upward-launch residual.
    _, ref, got, contacts = _run_row(6392, BHH)

    assert int(got["action_id"][0]) == int(ref["action_id"][0]) == 356
    assert int(contacts["wall_kind"][0]) == 2
    assert int(contacts["wall_id"][0]) == 9
    assert float(got["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-6)
    assert float(got["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)


@pytest.mark.integration
def test_specialairhi_right_wall_probe_does_not_overextend_fd_lip_control() -> None:
    # mpCheckRightWall uses raw wall endpoints plus mpLineIntersectionV's local endpoint clamp; it
    # must not use mpLib_8004ED5C's broader linked-line extension and steal this BODY row.
    _, ref, got, contacts = _run_row(2601, TCH)

    assert int(got["action_id"][0]) == int(ref["action_id"][0]) == 86
    assert int(got["hitlag"][0]) == int(ref["hitlag"][0]) == 5
    assert int(contacts["wall_kind"][0]) == 0
    assert float(got["pos_x"][0]) == pytest.approx(float(ref["pos_x"][0]), abs=1e-6)
    assert float(got["pos_y"][0]) == pytest.approx(float(ref["pos_y"][0]), abs=1e-6)


@pytest.mark.integration
def test_specialairhi_right_wall_owner_does_not_broaden_left_wall_control() -> None:
    # The current retained JObj/envelope owner is right-wall-only. LeftWall's symmetric
    # mpColl_80046224 envelope needs its own source-shaped predicate before it can be broadened.
    _, ref, got, contacts = _run_row(3099, GAT)

    assert int(got["action_id"][0]) == int(ref["action_id"][0]) == 356
    assert int(contacts["wall_kind"][0]) == 1
    assert int(contacts["wall_kind"][0]) != 2
