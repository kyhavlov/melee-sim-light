import random

from tools.eval import fuzz_live_clip


def test_live_clip_matrix_uses_manifest_registered_chars() -> None:
    assert fuzz_live_clip.matrix_chars() == ("fox", "falco", "marth", "sheik", "zelda")


def test_sheik_fod_ledgedeck_escapeair_sweep_stays_out_of_hull() -> None:
    # Live ledge -> Fall/JumpAerial -> EscapeAir sweep. The source owner is the carried
    # MSLSTG01 ledge floor's adjacent wall consumed by ftCo_EscapeAir_Coll -> mpColl_80046904.
    assert fuzz_live_clip.sweep_ledgedash("sheik", "fod") == []


def test_zelda_bf_ledgedeck_escapeair_boundary_sweep_stays_out_of_hull() -> None:
    # Same carried ledge-wall owner for Zelda's generic EscapeAir after Sheik<->Zelda support.
    assert fuzz_live_clip.sweep_boundary_approach("zelda", "bf") == []


def test_sheik_fd_escapeair_random_ledge_wall_regression_stays_out_of_hull() -> None:
    # Random episode 765 from a 20260621 Sheik/FD soak crossed FD's right cardinal ledge wall in
    # EscapeAir. The source owner is the same entry-callback mpCollPrev root sweep, but FD is
    # represented by the generated all-cardinal static-hard-floor stage topology instead of a
    # sloped ledge-wall shape.
    rng = random.Random(1354821142)
    start_x = rng.choice((55.0, 70.0, 78.0, 83.0, -70.0, -83.0))
    ep = fuzz_live_clip.Episode(rng_seed=1354821142, char="sheik", stage="fd", start_x=start_x)
    ep.script = fuzz_live_clip._policy_script(rng, 180) + [fuzz_live_clip._mk_inputs()] * 60
    assert fuzz_live_clip.run_episode(ep) == []
