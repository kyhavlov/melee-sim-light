"""Ratchet on raw cross-character action-id dispatch.

Char-range action ids (341+) mean DIFFERENT moves per character; generic engine code
that keys raw `MSL_ACT_FX_*` constants (gated by `msl_char_id_is_spacie`) silently
drives the wrong machine for every new character until hand-audited. The long-term
migration moves these onto extracted per-(char, action) MotionState owner class bits
(exemplar: MSL_MS_CLASS3_FX_SPECIALHI_HOLD_AIR_PHYS consumed in physics.c; see
motion_state_owners.h).

This test is a RATCHET, not a ban: per-file counts of `MSL_ACT_FX_` references and
`msl_char_id_is_spacie` gates must never INCREASE. Converting a site to class bits
(count goes down) requires updating the baseline downward. Files owned entirely by the
spacie machines (blaster.c, shine.c) and the id definition headers are exempt - raw
ids are their proper vocabulary.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Whitelisted: per-char special modules and id-definition/registry headers.
EXEMPT = {
    "action_ids.h",
    "blaster.c",
    "shine.c",
    "char_registry.h",
    "motion_state_owners.h",
}

# Baseline as of the retro-cleanup campaign (2026-06-11). Lower is better; raising any
# number needs an explicit review decision.
FX_BASELINE = {
    "anim_timebase.c": 2,
    "api.c": 16,
    "combat.c": 51,
    "damage_terminal_owner.h": 5,
    "hitboxes.c": 7,
    "hitlist.c": 2,
    "hurtboxes.c": 8,
    "instance_id.c": 2,
    "items.c": 36,
    "ledge.c": 7,
    "locomotion.c": 100,
    "mpcoll_env.c": 4,
    "mpcoll_ground.c": 30,
    "mpcoll_wall_ceil.c": 5,
    "physics.c": 25,
    "reflector_bubbles.c": 4,
    "shielddesc_geometry.h": 2,
    "specialhi_pose.h": 5,
    "state_flags.c": 12,
}

SPACIE_GATE_BASELINE = {
    "anim_timebase.c": 2,
    "api.c": 7,
    "combat.c": 28,
    "hitboxes.c": 1,
    "hitlist.c": 1,
    "hurtboxes.c": 1,
    "items.c": 10,
    "ledge.c": 1,
    "locomotion.c": 27,
    "mpcoll_env.c": 3,
    "mpcoll_ground.c": 23,
    "mpcoll_wall_ceil.c": 3,
    "physics.c": 12,
    "reflector_bubbles.c": 3,
    "shielddesc_geometry.h": 1,
    "state_flags.c": 3,
}


def _counts(pattern: str) -> dict[str, int]:
    rx = re.compile(pattern)
    out: dict[str, int] = {}
    for f in sorted((ROOT / "src").iterdir()):
        if f.suffix not in (".c", ".h") or f.name in EXEMPT:
            continue
        n = len(rx.findall(f.read_text(encoding="utf-8", errors="replace")))
        if n:
            out[f.name] = n
    return out


def test_raw_fx_action_id_references_do_not_grow() -> None:
    counts = _counts(r"MSL_ACT_FX_")
    for name, n in counts.items():
        assert n <= FX_BASELINE.get(name, 0), (
            f"src/{name}: raw MSL_ACT_FX_* references grew to {n} "
            f"(baseline {FX_BASELINE.get(name, 0)}). New per-(char, action) behavior must go "
            "through MotionState owner class bits (motion_state_owners.h), not raw FX ids."
        )


def test_spacie_predicate_gates_do_not_grow() -> None:
    counts = _counts(r"msl_char_id_is_spacie")
    for name, n in counts.items():
        assert n <= SPACIE_GATE_BASELINE.get(name, 0), (
            f"src/{name}: msl_char_id_is_spacie gates grew to {n} "
            f"(baseline {SPACIE_GATE_BASELINE.get(name, 0)}). Use MotionState owner class bits "
            "for new per-(char, action) dispatch."
        )
