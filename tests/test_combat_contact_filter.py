from __future__ import annotations

import numpy as np
import pytest

from tools.eval.dataset import SEED_DTYPE

# src/hitboxes_tables.h (MSLHITB1 u16_6 bits)
HIT_GROUNDED = 1 << 9
HIT_AERIAL = 1 << 10


def _make_handle(*, defender_on_ground: int):
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
    seed_view["num_players"][0] = np.uint8(2)
    seed_view["instance_id"][0, 0] = np.uint16(111)
    seed_view["instance_id"][0, 1] = np.uint16(222)
    seed_view["on_ground"][0, 1] = np.uint8(1 if defender_on_ground else 0)

    msl_binding.reseed_seed(handle, seed)
    return handle


def _force_single_overlap(*, handle, hitbox_flags: int):
    import msl_binding

    # Attacker: P0 hitbox 0 at origin.
    msl_binding.debug_clear_hitboxes_world(handle, 0, 0)
    msl_binding.debug_set_hitbox_world(handle, 0, 0, 0, 0.0, 0.0, 0.0, 1.0, 5.0, 1)
    msl_binding.debug_set_hitbox_flags(handle, 0, 0, 0, int(hitbox_flags))

    # Defender: P1 single capsule centered at origin.
    msl_binding.debug_clear_hurtcaps_world(handle, 0, 1)
    msl_binding.debug_set_hurtcap_world(handle, 0, 1, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5)


@pytest.mark.parametrize(
    ("defender_on_ground", "hitbox_flags"),
    [
        (1, HIT_AERIAL),  # grounded victim requires HIT_GROUNDED
        (0, HIT_GROUNDED),  # airborne victim requires HIT_AERIAL
    ],
)
def test_debug_combat_contacts_filtered_skips_ground_air_mismatch(
    defender_on_ground: int, hitbox_flags: int
) -> None:
    import msl_binding

    handle = _make_handle(defender_on_ground=defender_on_ground)
    try:
        _force_single_overlap(handle=handle, hitbox_flags=hitbox_flags)

        _, raw_count = msl_binding.debug_combat_contacts(handle, 0, 16)
        assert raw_count == 1

        _, filtered_count = msl_binding.debug_combat_contacts_filtered(handle, 0, 16)
        assert filtered_count == 0
    finally:
        msl_binding.destroy(handle)


@pytest.mark.parametrize(
    ("defender_on_ground", "hitbox_flags"),
    [
        (1, HIT_GROUNDED),
        (0, HIT_AERIAL),
    ],
)
def test_debug_combat_contacts_filtered_keeps_ground_air_match(
    defender_on_ground: int, hitbox_flags: int
) -> None:
    import msl_binding

    handle = _make_handle(defender_on_ground=defender_on_ground)
    try:
        _force_single_overlap(handle=handle, hitbox_flags=hitbox_flags)

        _, raw_count = msl_binding.debug_combat_contacts(handle, 0, 16)
        assert raw_count == 1

        _, filtered_count = msl_binding.debug_combat_contacts_filtered(handle, 0, 16)
        assert filtered_count == 1
    finally:
        msl_binding.destroy(handle)
