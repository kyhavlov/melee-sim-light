from __future__ import annotations

import numpy as np

from tools.eval.validation_dtypes import SEED_DTYPE


def test_combat_smoke_forced_overlap_reports_contacts() -> None:
    import msl_binding

    sizes = msl_binding.sizes()
    seed_stride = int(sizes["seed"])

    handle = msl_binding.init(
        batch_size=1,
        num_players=2,
        ucf_enabled=1,
        ucf_cardinals_1_0_enabled=1,
    )

    seed = np.zeros((1, seed_stride), dtype=np.uint8)
    seed_view = seed.view(SEED_DTYPE).reshape(-1)
    seed_view["stage_id"][0] = np.uint32(32)
    seed_view["num_players"][0] = np.uint8(2)
    seed_view["instance_id"][0, 0] = np.uint16(111)
    seed_view["instance_id"][0, 1] = np.uint16(222)

    msl_binding.reseed_seed(handle, seed)

    # Force overlapping primitives by writing world-space arrays directly.
    # Attacker: P0 hitbox 0 and 1 overlap victim; selection must pick hitbox_id=0.
    msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
    msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
    msl_binding.debug_set_hitbox_world(handle, 0, 0, 1, 0.0, 0.0, 0.0, 2.0, 999.0, 1)

    # Defender: P1 single capsule centered at origin.
    msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
    msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)

    # Debug contact dump sees intersections, in deterministic hitbox_id order.
    raw, count = msl_binding.debug_combat_contacts(handle, 0, 16)
    assert count == 2
    assert raw.shape[1] > 0

    contact_dtype = np.dtype(
        [
            ("attacker", "u1"),
            ("defender", "u1"),
            ("hitbox_id", "u1"),
            ("hurtcap_id", "u1"),
            ("attacker_msid", "<u2"),
            ("attacker_action_frame", "<i2"),
            ("hitbox_x", "<f4"),
            ("hitbox_y", "<f4"),
            ("hitbox_z", "<f4"),
            ("hitbox_radius", "<f4"),
            ("hitbox_damage", "<f4"),
            ("hurtcap_ax", "<f4"),
            ("hurtcap_ay", "<f4"),
            ("hurtcap_az", "<f4"),
            ("hurtcap_bx", "<f4"),
            ("hurtcap_by", "<f4"),
            ("hurtcap_bz", "<f4"),
            ("hurtcap_radius", "<f4"),
        ],
        align=False,
    )
    assert raw.shape[1] == contact_dtype.itemsize
    contacts = raw.reshape(-1).view(contact_dtype)[:count]
    assert int(contacts["attacker"][0]) == 0
    assert int(contacts["defender"][0]) == 1
    assert int(contacts["hitbox_id"][0]) == 0
    assert float(contacts["hitbox_damage"][0]) == 5.0
    assert float(contacts["hitbox_radius"][0]) == 1.0
    assert float(contacts["hurtcap_radius"][0]) == 0.5

    assert int(contacts["attacker"][1]) == 0
    assert int(contacts["defender"][1]) == 1
    assert int(contacts["hitbox_id"][1]) == 1
    assert float(contacts["hitbox_damage"][1]) == 999.0
