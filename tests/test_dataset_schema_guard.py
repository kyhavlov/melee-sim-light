from __future__ import annotations

from tools.eval.dataset import SAMPLE_DTYPE, SEED_DTYPE


def test_seed_schema_includes_staling_fields() -> None:
    # PP#4 groundwork: the seed schema must carry stale queue state through reseed.
    assert "stale_queue_index" in SEED_DTYPE.fields
    assert "stale_move_id" in SEED_DTYPE.fields
    assert "stale_attack_instance" in SEED_DTYPE.fields
    assert "attack_instance" in SEED_DTYPE.fields
    # Throw-side pulse-latch schema scaffold (future causal ownership wiring).
    assert "throw_pulse_consumed" in SEED_DTYPE.fields
    assert "throw_pulse_crossed_prev_frame" in SEED_DTYPE.fields
    assert "source_clear_timer_x18c8" in SEED_DTYPE.fields
    assert "source_clear_owner_set_phase" in SEED_DTYPE.fields
    assert "source_clear_grounded_damage_clear_phase" in SEED_DTYPE.fields
    assert "source_clear_terminal_phase" in SEED_DTYPE.fields
    # Walk callback-owned source velocity lane (ftWalkCommon_800DFDDC `mv_x0`).
    assert "walk_anim_source_vel_f32" in SEED_DTYPE.fields


def test_dataset_dtype_sizes_match_c_structs() -> None:
    # If these drift, preprocessing/eval will fail with record_size mismatches.
    import msl_binding

    sizes = msl_binding.sizes()
    assert int(sizes["seed"]) == SEED_DTYPE.itemsize
    assert int(sizes["sample"]) == SAMPLE_DTYPE.itemsize
