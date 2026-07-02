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


@pytest.mark.integration
def test_item_hit_damage_entry_faces_away_when_owner_left_of_victim() -> None:
    # Replay-real lock: item-hit damage entry should copy ftColl_8007A06C's item-facing sign so the
    # victim turns away from the item source. This GAT row's item and owner are both left of victim.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[923:924]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == 14  # Dash
    assert int(row["ref_t1"]["action_id"][0, p]) == 75  # DamageHi1
    assert float(row["seed_t"]["pos_x"][0, p]) > float(row["seed_t"]["pos_x"][0, 1])
    assert int(row["seed_t"]["facing"][0, p]) == 1
    assert int(row["ref_t1"]["facing"][0, p]) == 0

    binding = pytest.importorskip("msl_binding")
    out = _step_one_row(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["facing"][p]) == int(row["ref_t1"]["facing"][0, p])


@pytest.mark.integration
def test_item_hit_damage_entry_faces_away_when_owner_right_of_victim() -> None:
    # Replay-real lock: the same item-hit damage entry should face right when the item source is to
    # the victim's right at collision time.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/"
        "cardinal_1.0_recent/GracefulAttachedTurtle.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[3386:3387]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == 67  # AttackAirB
    assert int(row["ref_t1"]["action_id"][0, p]) == 84  # DamageAir1
    assert float(row["seed_t"]["pos_x"][0, p]) < float(row["seed_t"]["pos_x"][0, 1])
    assert int(row["seed_t"]["facing"][0, p]) == 0
    assert int(row["ref_t1"]["facing"][0, p]) == 1

    binding = pytest.importorskip("msl_binding")
    out = _step_one_row(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["facing"][p]) == int(row["ref_t1"]["facing"][0, p])


@pytest.mark.integration
def test_ordinary_live_falco_laser_body_damage_keeps_item_velocity_facing_dcc_392() -> None:
    # Replay-real non-ThrowLw control for ftColl_8007A06C's item BODY facing path:
    # a normal state-0 Falco laser owned by a non-throwing fighter hits Fox, and damage entry faces
    # away from the live laser velocity owner.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
    # refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}
    # refs/melee/src/melee/it/types.h::ItemCommonData::x78_float
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = (
        "replays/validation/"
        "aggregate_recent/DistinctCaringCobra.slpz"
    )
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[392:393]
    p = 0
    owner = 1
    slot = 0
    item = row["seed_t"]["items"][0, slot]

    assert int(row["seed_t"]["action_id"][0, owner]) not in (0x00DB, 0x00DC, 0x00DD, 0x00DE)
    assert int(item["exists"]) == 1
    assert int(item["type"]) == 55  # Falco laser
    assert int(item["state"]) == 0
    assert int(item["owner"]) == owner
    assert float(item["vel_x"]) > 0.0
    assert int(row["ref_t1"]["instance_hit_by"][0, p]) == int(item["instance_id"])
    assert int(row["ref_t1"]["action_id"][0, p]) == 84  # DamageAir1
    assert int(row["ref_t1"]["facing"][0, p]) == 0
    assert float(row["ref_t1"]["speed_x_attack"][0, p]) > 0.0

    binding = pytest.importorskip("msl_binding")
    out = _step_one_row(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["facing"][p]) == int(row["ref_t1"]["facing"][0, p])
    assert int(out["instance_hit_by"][p]) == int(row["ref_t1"]["instance_hit_by"][0, p])
    assert float(out["speed_x_attack"][p]) == pytest.approx(
        float(row["ref_t1"]["speed_x_attack"][0, p]), abs=1e-6
    )


@pytest.mark.integration
def test_stationary_illusion_item_damage_uses_item_position_for_facing_his_1673() -> None:
    # Replay-real positive for ftColl_8007A06C's item branch:
    # stationary/slow items use item->pos.x, not owner fighter pos.x, for fp->dmg.facing_dir_1.
    # HIS:1673 has Falco's stationary Phantasm item left of Fox; owner-root recomputation later in
    # the frame gives negative x attack speed, but source item-position facing matches vanilla
    # positive knockback.
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
    # refs/melee/src/melee/it/types.h::ItemCommonData::x78_float
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "replays/validation/aggregate_recent/HungryImportantSnake.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[1673:1674]
    p = 0
    item = row["seed_t"]["items"][0, 0]

    assert int(item["exists"]) == 1
    assert int(item["type"]) == 56
    assert int(item["state"]) == 1
    assert int(item["owner"]) == 1
    assert float(item["vel_x"]) == pytest.approx(0.0)
    assert float(item["pos_x"]) < float(row["seed_t"]["pos_x"][0, p])
    assert int(row["ref_t1"]["action_id"][0, p]) == 90
    assert float(row["ref_t1"]["speed_x_attack"][0, p]) > 0.0

    binding = pytest.importorskip("msl_binding")
    out = _step_one_row(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["action_id"][p]) == int(row["ref_t1"]["action_id"][0, p])
    assert int(out["hitlag"][p]) == int(row["ref_t1"]["hitlag"][0, p])
    assert int(out["hitstun"][p]) == int(row["ref_t1"]["hitstun"][0, p])
    assert int(out["facing"][p]) == int(row["ref_t1"]["facing"][0, p])
    assert float(out["speed_x_attack"][p]) == pytest.approx(
        float(row["ref_t1"]["speed_x_attack"][0, p]), abs=1e-6
    )


@pytest.mark.integration
def test_fresh_illusion_spawn_keeps_steady_item_facing_owner_out_of_scope_dsg_11089() -> None:
    # Replay-real negative/package-boundary lock:
    # ftColl_8007A06C's item-position facing owner is retained only for steady Illusion articles
    # that already exist at frame start. DSG:11089 spawns a Falco Illusion article during this step
    # and still has an adjacent Side-B callback/contact residual (action_id is not replay-exact).
    # Do not apply the steady item-position facing owner to that fresh-spawn phase.
    # refs/melee/src/melee/it/items/itfoxillusion.c::{it_8029CFF0,itFoxillusion_UnkMotion1_Phys}
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = "replays/validation/battlefield_recent/DelayedSuperbGuanaco.slpz"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {dataset_rel}")

    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[11089:11090]
    p = 0

    assert int(row["seed_t"]["action_id"][0, p]) == 67  # AttackAirB
    assert int(row["seed_t"]["items"][0, 0]["exists"]) == 0
    assert int(row["ref_t1"]["items"][0, 0]["exists"]) == 1
    assert int(row["ref_t1"]["items"][0, 0]["type"]) == 57  # Falco Illusion article
    assert int(row["ref_t1"]["facing"][0, p]) == 0

    binding = pytest.importorskip("msl_binding")
    out = _step_one_row(binding=binding, row=row, num_players=int(ds.num_players))
    assert int(out["facing"][p]) == int(row["ref_t1"]["facing"][0, p])
