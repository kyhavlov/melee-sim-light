"""Registry completeness: every registry character must run end-to-end.

The marth port found a class of silent per-char data degradation: char-blind table
caches (move_tables) and init-soft loaders (staling, shield tilt) that returned
NULL/defaults for an unregistered or artifact-less character instead of failing.
All per-char table inits are now strict at msl_binding.init time, so the loud
property this file locks is:

1. init itself proves every registry character's table artifacts load, and
2. each registry character can actually seed and step (registry row complete,
   ids consistent, anim/pose/ECB tables answer for its idle pose).

When adding a character, this test goes green only once the whole data pipeline
is in place - it is the unit-level tripwire for the porting guide's Phase 1 gate.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import pytest

from test_char_common_action_coverage import _mk_inputs, _seed_base  # noqa: E402
from tools.extraction.char_registry import CHARS  # noqa: E402
from tools.extraction.extract_character_attrs import (  # noqa: E402
    _resolved_can_walljump,
    _source_can_walljump,
)


def test_registry_walljump_flags_match_present_decomp() -> None:
    decomp = ROOT / "refs/melee"
    for name, info in CHARS.items():
        assert _source_can_walljump(name, melee_decomp=decomp) is info.can_walljump
        assert _resolved_can_walljump(name, melee_decomp=decomp) is info.can_walljump


def test_registry_walljump_flags_are_cwd_independent_without_decomp(tmp_path: Path) -> None:
    missing = tmp_path / "not-a-decomp"
    for name, info in CHARS.items():
        assert _source_can_walljump(name, melee_decomp=missing) is None
        assert _resolved_can_walljump(name, melee_decomp=missing) is info.can_walljump


def test_registry_walljump_source_disagreement_is_loud(tmp_path: Path) -> None:
    info = CHARS["marth"]
    source_dir = tmp_path / "src/melee/ft/chara" / info.decomp_dir
    source_dir.mkdir(parents=True)
    (source_dir / "fake.c").write_text("fp->can_walljump = true;\n", encoding="utf-8")
    with pytest.raises(RuntimeError, match="registry can_walljump=False disagrees"):
        _resolved_can_walljump("marth", melee_decomp=tmp_path)


@pytest.mark.parametrize("char_name", sorted(CHARS))
def test_registry_char_inits_seeds_and_steps(char_name: str) -> None:
    import msl_binding

    info = CHARS[char_name]
    sizes = msl_binding.sizes()
    # init is strict: every registry character's per-char tables (params, msids,
    # staling, attack-id, motion-state owners, anim, pose, shield tilt, hurtcaps,
    # move tables, hitboxes, ecb) must have loaded or this returns NULL/raises.
    handle = msl_binding.init(batch_size=1, num_players=2)
    try:
        # build the seed directly from the registry (the coverage helper's CHARS dict
        # excludes chars from its own param list; this test must cover ALL registry rows)
        seed = _seed_base("fox", grounded=True)
        seed["char_id"][0, 0] = np.uint8(info.internal_id)
        assert int(seed["char_id"][0, 0]) == info.internal_id
        msl_binding.reseed_seed(handle, seed.view(np.uint8).reshape((1, int(sizes["seed"]))))
        prev = _mk_inputs()
        for _ in range(10):
            inp = _mk_inputs(main_x=80)
            msl_binding.step_input(handle, prev, inp)
            prev = inp
        ob = np.zeros((1, int(sizes["compare"])), dtype=np.uint8)
        msl_binding.write_compare(handle, ob)
        from test_char_common_action_coverage import COMPARE_DTYPE

        out = ob.view(COMPARE_DTYPE).reshape((1,))[0]
        # A walking input on a grounded idle seed must move the fighter: proves the
        # char's anim/locomotion tables answered (not silent-default zeros).
        assert abs(float(out["pos_x"][0])) > 0.1, (
            f"{char_name}: no locomotion after 10 walk frames - per-char tables "
            "likely returned silent defaults"
        )
        assert int(out["stocks"][0]) == 4
    finally:
        msl_binding.destroy(handle)
