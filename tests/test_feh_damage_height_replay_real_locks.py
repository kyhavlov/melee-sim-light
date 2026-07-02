from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tests.test_combat_ownership_seed_guardrail_locks import (
    _run_one_step_row,
    _skip_if_required_artifacts_missing,
)
from tools.eval.validation_dtypes import COMPARE_DTYPE
from tests.replay_buffers_loader import load_replay_buffers


_FEH = Path("replays/validation/dream_land_recent/FlippantEnchantedHorse.slpz")

_CONTACT_CLASSIFIED_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("contact_kind", "u1"),
        ("hurtcap_id", "u1"),
        ("_pad0", "u1", (3,)),
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
        ("shield_x", "<f4"),
        ("shield_y", "<f4"),
        ("shield_z", "<f4"),
        ("shield_radius", "<f4"),
    ],
    align=False,
)


def test_attackairn_primary_phantom_allows_later_medium_damage_height_feh_12440() -> None:
    # FEH 12440 is a same-frame aerial trade:
    # - Falco AttackAirN hb0 only grazes Fox cap2/head-high inside the x7A8 phantom/tip-log range.
    # - Source ftColl_80078C70 advances to the later same-group hb1 full BODY overlap, so
    #   Fighter_ProcessHit consumes hb1's selected medium-cap x184c and enters DamageFlyN.
    # refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _FEH
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_FEH}")

    seed, ref, out = _run_one_step_row(dataset_path, 12440, 0)
    assert int(seed["action_id"][0]) == 356  # ftFx_MS_SpecialAirHi
    assert int(seed["action_id"][1]) == 69  # ftCo_MS_AttackAirN
    assert int(ref["action_id"][0]) == 88  # ftCo_MS_DamageFlyN
    assert int(out["action_id"][0]) == int(ref["action_id"][0])
    assert int(out["animation_index"][0]) == int(ref["animation_index"][0])
    assert int(out["hitlag"][0]) == int(ref["hitlag"][0])


def test_attackairn_primary_full_overlap_keeps_high_damage_height_feh_12440_negative() -> None:
    # Adjacent geometry negative for the source-order owner above: when the same primary hb0 is
    # moved deeper into cap2 so its selected source contact is no longer phantom-range, hb0 keeps
    # the full BODY DmgLog and the high-cap DamageFlyHi result. This proves the owner is not a
    # broad AttackAirN/SpecialAirHi medium-height override.
    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)
    dataset_path = root / _FEH
    if not dataset_path.exists():
        pytest.skip(f"missing local replay: {_FEH}")

    binding = pytest.importorskip("msl_binding")
    ds = load_replay_buffers(str(dataset_path))
    row = ds.rows[12440:12441]
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, seed_stride
    )
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.num_players))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.debug_step_input_pre_combat(handle, prev_input_bytes, input_bytes)
        raw, count = binding.debug_combat_contacts_classified(handle, 0, 256)
        contacts = raw.reshape(-1).view(_CONTACT_CLASSIFIED_DTYPE)[:count]
        hb0_cap2 = [
            c
            for c in contacts
            if int(c["contact_kind"]) == 0
            and int(c["attacker"]) == 1
            and int(c["defender"]) == 0
            and int(c["hitbox_id"]) == 0
            and int(c["hurtcap_id"]) == 2
        ]
        assert len(hb0_cap2) == 1
        hb0_radius = float(hb0_cap2[0]["hitbox_radius"])
        hb0_damage = float(hb0_cap2[0]["hitbox_damage"])

        # Isolate the selected-height boundary while preserving p1's live AttackAirN hitbox
        # metadata (hit group, angle, KB, flags). hb0 is a full overlap on a high cap; hb1 is a
        # later same-group full overlap on a medium cap. Source order should keep hb0 as the full
        # BODY DmgLog because the primary contact is no longer in phantom range.
        binding.debug_clear_hitboxes_world(handle, 0, 0)
        binding.debug_clear_hurtcaps_world(handle, 0, 0)
        binding.debug_set_hurtcap_world(handle, 0, 0, 0, -0.5, 0.0, 0.0, 0.5, 0.0, 0.0, 2.0)
        binding.debug_set_hurtcap_height(handle, 0, 0, 0, 2)
        binding.debug_set_hurtcap_world(handle, 0, 0, 1, 19.5, 0.0, 0.0, 20.5, 0.0, 0.0, 2.0)
        binding.debug_set_hurtcap_height(handle, 0, 0, 1, 1)
        binding.debug_set_hitbox_world(handle, 0, 1, 0, 0.0, 0.0, 0.0, hb0_radius, hb0_damage, 1)
        binding.debug_set_hitbox_world(handle, 0, 1, 1, 20.0, 0.0, 0.0, hb0_radius, hb0_damage, 1)
        binding.debug_combat_resolve(handle)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]
    assert int(out["action_id"][0]) == 87  # ftCo_MS_DamageFlyHi
    assert int(out["animation_index"][0]) == 177  # ftCo_SM_DamageFlyHi
