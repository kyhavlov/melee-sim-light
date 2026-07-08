from tools.eval import fuzz_live_clip


def test_live_clip_matrix_uses_manifest_registered_chars() -> None:
    assert fuzz_live_clip.matrix_chars() == (
        "fox",
        "falco",
        "marth",
        "falcon",
        "puff",
        "sheik",
        "zelda",
    )


def test_sheik_fod_ledgedeck_escapeair_sweep_stays_out_of_hull() -> None:
    # Live ledge -> Fall/JumpAerial -> EscapeAir sweep. One reseed at episode start keeps stale
    # CollData floor/wall owners observable.
    assert fuzz_live_clip.sweep_ledgedash("sheik", "fod") == []


def test_sheik_legal_stage_ledgedeck_escapeair_sweep_stays_out_of_hull() -> None:
    # Ledge -> jump/airdodge sweeps across every supported legal-stage shell. This is intentionally
    # a live-path fuzzer call: one reseed at episode start, then normal step_input frames, so stale
    # CollData floor/wall owners cannot be hidden by row-by-row reseeding.
    for stage in ("fd", "bf", "dl", "ys", "fod", "ps"):
        assert fuzz_live_clip.sweep_ledgedash("sheik", stage) == []


def test_zelda_bf_boundary_sweep_stays_out_of_hull() -> None:
    assert fuzz_live_clip.sweep_boundary_approach("zelda", "bf") == []


def test_zelda_bf_boundary_escapeair_drift_95_stays_out_of_hull() -> None:
    repro = fuzz_live_clip.zelda_bf_boundary_escapeair_repro(95)
    assert fuzz_live_clip._run_case(fuzz_live_clip.seed_for_repro(repro), repro.script, repro.stage) == []


def test_zelda_bf_boundary_escapeair_drift_127_stays_out_of_hull() -> None:
    repro = fuzz_live_clip.zelda_bf_boundary_escapeair_repro(127)
    assert fuzz_live_clip._run_case(fuzz_live_clip.seed_for_repro(repro), repro.script, repro.stage) == []


def test_sheik_fd_escapeair_random_ledge_wall_regression_stays_out_of_hull() -> None:
    repro = fuzz_live_clip.sheik_fd_seed_1354821142_repro()
    assert fuzz_live_clip._run_case(fuzz_live_clip.seed_for_repro(repro), repro.script, repro.stage) == []


def test_sheik_non_fd_random_carried_ledge_wall_repros_stay_out_of_hull() -> None:
    for stage, seed in (("dl", 856430243), ("ps", 856430243)):
        repro = fuzz_live_clip.random_policy_clip_repro("sheik", stage, seed)
        assert fuzz_live_clip._run_case(
            fuzz_live_clip.seed_for_repro(repro), repro.script, repro.stage) == []


def test_sheik_fod_random_pass_pass_regression_lock_stays_out_of_hull() -> None:
    # This case passed on HEAD-equivalent runtime under the final oracle. Keep it as PASS/PASS
    # regression coverage for the carried ledge-floor wall owner, not as a fixed pre/post repro.
    repro = fuzz_live_clip.sheik_fod_random_648177039_regression_lock()
    assert fuzz_live_clip._run_case(fuzz_live_clip.seed_for_repro(repro), repro.script, repro.stage) == []
