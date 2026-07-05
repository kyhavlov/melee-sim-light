import random

import pytest

from tools.eval import fuzz_live_clip


def test_live_clip_matrix_uses_manifest_registered_chars() -> None:
    assert fuzz_live_clip.matrix_chars() == ("fox", "falco", "marth", "sheik", "zelda")


def test_sheik_fod_ledgedeck_escapeair_sweep_stays_out_of_hull() -> None:
    # Coverage-only live ledge -> Fall/JumpAerial -> EscapeAir sweep. This is not proof for the
    # missing user-reported Sheik clip artifact.
    assert fuzz_live_clip.sweep_ledgedash("sheik", "fod") == []


def test_sheik_legal_stage_ledgedeck_escapeair_sweep_stays_out_of_hull() -> None:
    # Ledge -> jump/airdodge sweeps across every supported legal-stage shell. This is intentionally
    # a live-path fuzzer call: one reseed at episode start, then normal step_input frames, so stale
    # CollData floor/wall owners cannot be hidden by row-by-row reseeding.
    for stage in ("fd", "bf", "dl", "ys", "fod", "ps"):
        assert fuzz_live_clip.sweep_ledgedash("sheik", stage) == []


@pytest.mark.xfail(
    reason=(
        "coverage-only Zelda/BF boundary clip lead remains; not proof for the missing "
        "user-reported Sheik clip artifact"
    )
)
def test_zelda_bf_ledgedeck_escapeair_boundary_sweep_stays_out_of_hull() -> None:
    # Coverage-only boundary sweep for Zelda's generic EscapeAir after Sheik<->Zelda support. This
    # remains separate from the missing user-reported Sheik clip artifact; do not use it as proof of
    # that bug being fixed.
    assert fuzz_live_clip.sweep_boundary_approach("zelda", "bf") == []


@pytest.mark.xfail(
    reason=(
        "coverage-only random Sheik/FD EscapeAir hull-interior lead remains; exact reported "
        "clip artifact/input is missing"
    )
)
def test_sheik_fd_escapeair_random_ledge_wall_regression_stays_out_of_hull() -> None:
    # Coverage-only random Sheik/FD ledge episode retained as a hull-interior guard. The exact
    # user-reported clip input/artifact is still missing, so this does not claim that fix.
    rng = random.Random(1354821142)
    start_x = rng.choice((55.0, 70.0, 78.0, 83.0, -70.0, -83.0))
    ep = fuzz_live_clip.Episode(rng_seed=1354821142, char="sheik", stage="fd", start_x=start_x)
    ep.script = fuzz_live_clip._policy_script(rng, 180) + [fuzz_live_clip._mk_inputs()] * 60
    assert fuzz_live_clip.run_episode(ep) == []
