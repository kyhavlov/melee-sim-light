from __future__ import annotations

import importlib
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

_THROW_LW = 222
_THROWN_LW = 242


def _one_step(binding, num_players, row):
    sizes = binding.sizes()
    ss = int(sizes["seed"])
    ins = int(sizes["input"])
    cs = int(sizes["compare"])
    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        binding.reseed_seed(handle, row["seed_t"].view(np.uint8).reshape((1, ss)).copy())
        binding.step_input(
            handle,
            row["prev_input_t"].view(np.uint8).reshape((1, ins)).copy(),
            row["input_t"].view(np.uint8).reshape((1, ins)).copy(),
        )
        ob = np.zeros((1, cs), dtype=np.uint8)
        binding.write_compare(handle, ob)
        return ob.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)


@pytest.mark.integration
def test_throwlw_swing_hit_seed_prevents_reseed_rehit_and_admits_first_hit() -> None:
    # The Sheik down-throw (ThrowLw, SM250) f31 create_hitbox (5%) hits the grabbed victim once; the f36
    # set_throw_flags release (3%) then pops them into DamageFlyTop. The throw-swing hit is reconstructed
    # into the thrower's per-hitbox seed hitlist (msl_derive_combat_hitlist_seed_fields), so a one-step
    # reseed INSIDE the still-active f31 hitbox window does not re-apply the (staled) hit. This locks:
    #   - positive: a mid-window ThrownLw frame reseeds with the victim percent unchanged and the
    #     thrower's spurious throw-swing hitlag absent;
    #   - adjacent negative: the frame where the hit first lands still admits it (the seed hitlist is
    #     empty there, so the hit fires and the percent rises) -- the reconstruction does not
    #     over-suppress.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c (throw-swing create_hitbox vs grabbed victim)
    root = Path(__file__).resolve().parents[1]
    rel = "datasets/sheik/replays/validation/sheik/AttractiveAnyClam.msl"
    path = root / rel
    if not path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    ds = read_dataset(str(path))
    samples = ds.samples
    num_players = int(ds.header["num_players"])
    n = int(samples.shape[0])

    binding = importlib.import_module("msl_binding")

    def thrower_victim(seed):
        for a in range(num_players):
            for d in range(num_players):
                if d == a:
                    continue
                if int(seed["action_id"][a]) == _THROW_LW and int(seed["action_id"][d]) == _THROWN_LW:
                    return a, d
        return None

    seeded = []  # frames where the throw-swing per-hitbox seed is present (reconstruction fired)
    first_hits = []  # admit frames: the swing hit lands this step and the per-hitbox seed is not yet set
    for i in range(1, n - 1):
        seed = samples[i]["seed_t"]
        ref = samples[i]["ref_t1"]
        tv = thrower_victim(seed)
        if tv is None:
            continue
        a, d = tv
        seed_present = any(int(v) for v in seed["combat_hitlist_hb_valid"][a])
        if seed_present:
            seeded.append((i, a, d))
        if float(ref["percent"][d]) > float(seed["percent"][d]) + 1e-3 and not seed_present:
            first_hits.append((i, a, d))

    assert seeded, "no seeded ThrowLw swing window found in dataset"

    # Positive: a representative mid-window seeded frame must NOT re-apply the swing hit on reseed.
    i, a, d = seeded[len(seeded) // 2]
    out = _one_step(binding, num_players, samples[i : i + 1])
    ref = samples[i]["ref_t1"]
    assert float(out["percent"][d]) == pytest.approx(float(ref["percent"][d]), abs=1e-3), (
        i,
        float(out["percent"][d]),
        float(ref["percent"][d]),
    )
    assert int(out["hitlag"][a]) == int(ref["hitlag"][a]), (i, int(out["hitlag"][a]))

    # Adjacent negative: the admit frame (seed hitlist empty) must still let the hit fire on reseed.
    assert first_hits, "no throw-swing admit frame found"
    j, aj, dj = first_hits[0]
    out2 = _one_step(binding, num_players, samples[j : j + 1])
    refj = samples[j]["ref_t1"]
    assert float(out2["percent"][dj]) == pytest.approx(float(refj["percent"][dj]), abs=1e-3), (
        j,
        float(out2["percent"][dj]),
        float(refj["percent"][dj]),
    )
    assert float(refj["percent"][dj]) > float(samples[j]["seed_t"]["percent"][dj]) + 1e-3


def _derive_throwlw_scenario(instance_hit_by_victim):
    # Synthetic ThrowLw window: thrower(p0) = Sheik(char 7) ThrowLw(222)/SM250 with the action frame in
    # the f31 swing-hitbox-active window; victim(p1) = ThrownLw(242) whose percent rises mid-window.
    # `instance_hit_by_victim` is None (absent) or an int (the per-frame victim instance_hit_by).
    from tools.slippi.combat_history import derive_combat_hitlist_seed_fields

    nf, w = 8, 2
    u8 = lambda v=0: np.full((nf, w), v, dtype=np.uint8)  # noqa: E731
    u16 = lambda v=0: np.full((nf, w), v, dtype=np.uint16)  # noqa: E731
    f32 = lambda v=0.0: np.full((nf, w), v, dtype=np.float32)  # noqa: E731

    char_id = u8(7)  # Sheik internal id
    action_id = u16()
    action_id[:, 0] = _THROW_LW
    action_id[:, 1] = _THROWN_LW
    animation_index = np.zeros((nf, w), dtype=np.uint32)
    animation_index[:, 0] = 250  # SM_ThrowLw (carries the f31 create_hitbox)
    animation_index[:, 1] = 242
    action_frame = np.zeros((nf, w), dtype=np.int16)
    action_frame[:, 0] = np.arange(29, 29 + nf, dtype=np.int16)  # walk through the f31..f36 swing window
    instance_id = u16()
    instance_id[:, 0] = 31
    instance_id[:, 1] = 32
    percent = f32()
    percent[3:, 1] = 5.0  # the swing hit lands at frame 3

    kwargs = dict(
        num_players=2,
        is_teams=False,
        team_id=u8(),
        char_id=char_id,
        action_id=action_id,
        action_frame=action_frame,
        animation_index=animation_index,
        facing=u8(1),
        on_ground=u8(1),
        pos_x=f32(),
        pos_y=f32(),
        fighter_scale_y=f32(1.0),
        guard_tilt_x8=u16(),
        guard_tilt_x4=f32(),
        stocks=u8(4),
        shield_hp=f32(),
        hurtbox_state=u8(),
        hitlag=u16(),
        instance_id=instance_id,
        input_buttons=u16(),
        input_l=u8(),
        input_r=u8(),
        percent=percent,
        include_per_hitbox=True,
        include_replay_only_body_admission=True,
    )
    if instance_hit_by_victim is not None:
        ihb = u16()
        ihb[:, 1] = instance_hit_by_victim
        kwargs["instance_hit_by"] = ihb
    (_cd, _iid, hb_valid, _hbcd, _hbiid, _shk) = derive_combat_hitlist_seed_fields(**kwargs)
    # hb_valid shape: (nf, players, hitboxes); return whether the thrower(p0) ever got a per-hitbox seed.
    return int(np.asarray(hb_valid)[:, 0, :].sum())


def test_throw_swing_seed_requires_matching_instance_hit_by_pairing() -> None:
    # Strictness lock: the reconstruction is only safe because it pairs the thrown victim to the thrower
    # by instance_hit_by == thrower instance_id. With a MATCHING pairing the per-hitbox seed is produced;
    # with a MISMATCHED pairing (or an ABSENT instance_hit_by signal) it must NOT be produced, even
    # though the throw/thrown actions and the percent edge are identical.
    matched = _derive_throwlw_scenario(instance_hit_by_victim=31)  # == thrower instance_id
    assert matched > 0, "matching-pairing throw window did not produce the per-hitbox seed"

    mismatched = _derive_throwlw_scenario(instance_hit_by_victim=99)  # != thrower instance_id
    assert mismatched == 0, "mismatched instance_hit_by must not produce a throw-swing seed"

    absent = _derive_throwlw_scenario(instance_hit_by_victim=None)  # no instance_hit_by signal at all
    assert absent == 0, "absent instance_hit_by must not produce a throw-swing seed"


def test_throw_swing_hit_seed_does_not_leak_outside_throw_context() -> None:
    # Adjacent negative on the reconstruction itself: the new throw-swing per-hitbox seed (a permanent
    # no-rehit cd==0xFFFF against a thrown victim) must never be present unless the owner is mid-throw.
    # This guards against over-seeding ordinary (non-throw) hitbox frames.
    root = Path(__file__).resolve().parents[1]
    rel = "datasets/sheik/replays/validation/sheik/AttractiveAnyClam.msl"
    path = root / rel
    if not path.exists():
        pytest.skip(f"missing local dataset: {rel}")
    samples = read_dataset(str(path)).samples
    n = int(samples.shape[0])
    throw_actions = set(range(219, 223))  # THROW_F..THROW_LW
    leaks = 0
    for i in range(n):
        seed = samples[i]["seed_t"]
        num_players = seed["action_id"].shape[0]
        for a in range(num_players):
            if int(seed["action_id"][a]) in throw_actions:
                continue
            hbv = seed["combat_hitlist_hb_valid"][a]
            hbcd = seed["combat_hitlist_hb_cd"][a]
            for hb in range(len(hbv)):
                if not int(hbv[hb]):
                    continue
                for d in range(hbcd.shape[1]):
                    if int(hbcd[hb, d]) == 0xFFFF and 239 <= int(seed["action_id"][d]) <= 243:
                        leaks += 1
    assert leaks == 0, f"throw-swing seed leaked into {leaks} non-throw frames"
