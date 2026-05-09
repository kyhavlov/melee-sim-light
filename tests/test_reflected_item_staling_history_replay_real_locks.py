from __future__ import annotations

from argparse import Namespace
from pathlib import Path

import numpy as np
import pytest

from tests.test_platform_collision_runtime import _step_one_replay_row
from tools.eval.dataset import read_dataset


def test_staling_history_prefers_fighter_instance_over_same_iid_item_control() -> None:
    # Control for native stale-history attribution: a live item/projectile row can share an
    # instance id with the damaging fighter action in replay-derived seeds, but source only calls
    # plStale_UpdateStaleMovesFromItem when the hit is item-owned. A normal fighter hit must keep
    # plStale_UpdateStaleMovesFromFighter ownership even if an item row has the same iid.
    # refs/melee/src/melee/pl/plstale.c::{
    #   plStale_UpdateStaleMovesFromFighter,plStale_UpdateStaleMovesFromItem}
    binding = pytest.importorskip("msl_binding")

    src_ports = [1, 2]
    char_id = np.array([[1, 22], [1, 22]], dtype=np.uint8)
    action_id = np.array([[0x0041, 0x000E], [0x0041, 0x000E]], dtype=np.uint16)
    action_frame = np.array([[1.0, 1.0], [2.0, 2.0]], dtype=np.float32)
    animation_index = np.array([[0, 0], [0, 0]], dtype=np.uint32)
    percent = np.array([[0.0, 0.0], [0.0, 5.0]], dtype=np.float32)
    stocks = np.array([[4, 4], [4, 4]], dtype=np.uint8)
    instance_id = np.array([[1234, 2000], [1234, 2000]], dtype=np.uint16)
    last_hit_by = np.array([[0xFF, 0xFF], [0xFF, 0]], dtype=np.uint8)
    last_hit_by_instance = np.array([[0, 0], [0, 1234]], dtype=np.uint16)

    item_exists = np.array([[1], [1]], dtype=np.uint8)
    item_owner = np.array([[0], [0]], dtype=np.int8)
    item_instance_id = np.array([[1234], [1234]], dtype=np.uint16)
    item_attack_id = np.array([[18], [18]], dtype=np.uint16)
    item_attack_instance = np.array([[99], [99]], dtype=np.uint16)

    attack_id_out, attack_inst_out, qi_out, stale_mid_out, stale_inst_out = (
        binding.derive_staling_history(
            src_ports,
            char_id,
            action_id,
            action_frame,
            animation_index,
            percent,
            stocks,
            instance_id,
            last_hit_by,
            last_hit_by_instance,
            item_exists,
            item_owner,
            item_instance_id,
            item_attack_id,
            item_attack_instance,
        )
    )

    assert int(qi_out[1, 0]) == 1
    assert int(stale_mid_out[1, 0, 0]) == int(attack_id_out[1, 0])
    assert int(stale_mid_out[1, 0, 0]) != 18
    assert int(stale_inst_out[1, 0, 0]) == int(attack_inst_out[1, 0])
    assert int(stale_inst_out[1, 0, 0]) != 99


@pytest.mark.integration
def test_specialn_laser_spawn_latches_live_blaster_attack_identity_replay_real(tmp_path: Path) -> None:
    # AGN has a fresh SpecialN laser that first appears in Slippi after Fox has already left
    # Blaster Loop. In source, Item_80268B18 -> it_8027B0C4 copies xD88/xD8C at the live
    # ftFx_SpecialN_CreateBlasterShot callback before that post-frame serialization point. The
    # data-backed laser shot-kind seed derivation must therefore use the previous owner identity
    # only for this first-visibility timing gap, so the first hit enters Fox's stale queue and the
    # later same-move laser is properly staled.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_CreateBlasterShot
    # refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C504
    # refs/melee/src/melee/it/it_2725.c::{it_8027B0C4,it_8027B070}
    root = Path(__file__).resolve().parents[1]
    slp = root / "replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slp"
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    from tools.slippi.make_dataset_from_slp import _main_impl

    out_path = tmp_path / "AttachedGoodNaturedGuanaco.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports=None,
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))
    samples = ds.samples

    first_visible_seed = samples[175]["seed_t"]
    shot = first_visible_seed["items"][2]
    assert int(first_visible_seed["action_id"][0]) == 0x002A  # Fox has already left SpecialNLoop.
    assert int(first_visible_seed["attack_id"][0]) == 1
    assert int(shot["exists"]) == 1
    assert int(shot["type"]) == 55  # generated laser shot kind from data/items/lasers.bin
    assert int(shot["owner"]) == 0
    assert int(shot["attack_id"]) == 18
    assert int(shot["attack_instance"]) == int(samples[174]["seed_t"]["attack_instance"][0])

    first_hit_seed = samples[178]["seed_t"]
    first_hit_ref = samples[178]["ref_t1"]
    assert int(first_hit_ref["instance_hit_by"][1]) == int(first_hit_seed["items"][1]["instance_id"])
    assert float(first_hit_ref["percent"][1]) == pytest.approx(3.0, abs=1e-6)

    post_first_hit_seed = samples[179]["seed_t"]
    assert int(post_first_hit_seed["stale_queue_index"][0]) == 1
    assert int(post_first_hit_seed["stale_move_id"][0][0]) == 18
    assert int(post_first_hit_seed["stale_attack_instance"][0][0]) == int(shot["attack_instance"])

    second_hit = samples[397]
    out = _step_one_replay_row(ds, 397)
    ref = second_hit["ref_t1"]
    assert int(out["hitlag"][1]) == int(ref["hitlag"][1]) == 3
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]), abs=1e-6)


@pytest.mark.integration
def test_reflected_item_hit_advances_new_owner_stale_queue_replay_real(tmp_path: Path) -> None:
    # EWT has a powershield-reflected laser that damages Falco at record 9898. The hit source is
    # item-owned in vanilla: plStale_UpdateStaleMovesFromItem uses the reflected item's current
    # owner plus its spawn-latched xD88/xD8C attack identity. This must advance Fox's stale queue
    # even though Fox's visible GuardReflect fighter action has FtMoveId_Default.
    #
    # The downstream lock at record 10380 is the first Fox Bair after that reflected laser. Without
    # the item-owned stale update, the Bair is over-staled (0.97 instead of 0.99), causing lower
    # percent/KB and a later DownBound edge exit in rollout.
    # refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078998,ftColl_80077464}
    root = Path(__file__).resolve().parents[1]
    slp = root / "replays/validation/fountain_of_dreams_recent/ElatedWearyTermite.slp"
    if not slp.exists():
        pytest.skip(f"missing local replay: {slp}")

    from tools.slippi.make_dataset_from_slp import _main_impl

    out_path = tmp_path / "ElatedWearyTermite.msl"
    _main_impl(
        Namespace(
            slp=str(slp),
            out=str(out_path),
            ports=None,
            ucf_enabled=True,
            ucf_cardinals_1_0_enabled=True,
        )
    )
    ds = read_dataset(str(out_path))
    samples = ds.samples

    reflect_seed = samples[9898]["seed_t"]
    reflect_ref = samples[9898]["ref_t1"]
    assert int(reflect_seed["action_id"][0]) == 0x00B6  # GuardReflect
    assert int(reflect_seed["attack_id"][0]) == 1  # fighter action remains FtMoveId_Default
    assert float(reflect_ref["percent"][1]) > float(reflect_seed["percent"][1])
    assert int(reflect_seed["items"][0]["exists"]) == 1
    assert int(reflect_seed["items"][0]["owner"]) == 0
    assert int(reflect_seed["items"][0]["instance_id"]) == int(reflect_ref["instance_hit_by"][1])
    assert int(reflect_seed["items"][0]["attack_id"]) == 18  # reflected laser SpecialN item owner

    post_reflect_seed = samples[9899]["seed_t"]
    assert int(post_reflect_seed["stale_queue_index"][0]) == 3
    assert int(post_reflect_seed["stale_move_id"][0][2]) == 18
    assert int(post_reflect_seed["stale_attack_instance"][0][2]) == int(
        reflect_seed["items"][0]["attack_instance"]
    )

    bair_row = samples[10380]
    assert int(bair_row["seed_t"]["stale_queue_index"][0]) == 3
    assert int(bair_row["seed_t"]["action_id"][0]) == 0x0043  # AttackAirB
    assert int(bair_row["seed_t"]["attack_id"][0]) == 15

    out = _step_one_replay_row(ds, 10380)
    ref = bair_row["ref_t1"]
    assert int(out["action_id"][1]) == int(ref["action_id"][1]) == 87  # DamageFlyHi
    assert float(out["percent"][1]) == pytest.approx(float(ref["percent"][1]), abs=1e-6)
    assert float(out["speed_x_attack"][1]) == pytest.approx(float(ref["speed_x_attack"][1]), abs=5e-7)
    assert float(out["speed_y_attack"][1]) == pytest.approx(float(ref["speed_y_attack"][1]), abs=5e-7)

    # Downstream source boundary exposed by the corrected KB: DownBound floor persistence crosses
    # FoD's generated right-lip slope/flat seam. The slope row must snap root Y to the generated
    # floor line, while the following flat-edge row stays airborne and only carries the source
    # floor id/root height through the floor-loss handoff.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    # refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    # refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B108,mplib.c::mpLib_8004DD90_Floor}
    out_10408 = _step_one_replay_row(ds, 10408)
    ref_10408 = samples[10408]["ref_t1"]
    assert int(out_10408["action_id"][1]) == int(ref_10408["action_id"][1]) == 191
    assert int(out_10408["ground_id"][1]) == int(ref_10408["ground_id"][1]) == 6
    assert int(out_10408["on_ground"][1]) == int(ref_10408["on_ground"][1]) == 1
    assert float(out_10408["pos_y"][1]) == pytest.approx(float(ref_10408["pos_y"][1]), abs=5e-7)

    out_10409 = _step_one_replay_row(ds, 10409)
    ref_10409 = samples[10409]["ref_t1"]
    assert int(out_10409["action_id"][1]) == int(ref_10409["action_id"][1]) == 191
    assert int(out_10409["ground_id"][1]) == int(ref_10409["ground_id"][1]) == 7
    assert int(out_10409["on_ground"][1]) == int(ref_10409["on_ground"][1]) == 0
    assert float(out_10409["pos_y"][1]) == pytest.approx(float(ref_10409["pos_y"][1]), abs=1e-7)
