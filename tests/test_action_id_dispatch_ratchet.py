"""Ratchet on raw cross-character action-id dispatch.

Char-range action ids (341+) mean DIFFERENT moves per character; generic engine code
that keys raw `MSL_ACT_FX_*` constants (gated by `msl_char_id_is_spacie`) silently
drives the wrong machine for every new character until hand-audited. The long-term
migration moves these onto extracted per-(char, action) MotionState owner class bits
(exemplar: MSL_MS_CLASS3_FX_SPECIALHI_HOLD_AIR_PHYS consumed in physics.c; see
motion_state_owners.h).

This test is a RATCHET, not a ban: per-file counts of `MSL_ACT_FX_` references and
`msl_char_id_is_spacie` gates must never INCREASE. Converting a site to kind identity
(count goes down) requires updating the baseline downward. The engine is now at ZERO
raw FX action ids outside the id-definition/registry headers and two documented api.c
table keys: every special machine (locomotion.c, shine.c, blaster.c) is kind-keyed
end to end via msl_motion_state_fx_special_kind / msl_motion_state_action_for_fx_kind.
"""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Whitelisted: per-char special modules and id-definition/registry headers.
# action_ids.h is NOT blanket-exempt: its enum DEFINITIONS are stripped before counting
# (see _counts), but behavior helpers in that header are counted like any engine code.
EXEMPT = {
    "char_registry.h",
    "motion_state_owners.h",
}

# Lines that are pure enumerator definitions (the id vocabulary itself), stripped from
# action_ids.h before counting so the ratchet sees only behavior code.
_ENUMERATOR_DEF_RE = re.compile(r"^\s*MSL_ACT_FX_[A-Z_0-9]+ = 0x[0-9A-Fa-f]+u?,")

# Baseline as of the retro-cleanup campaign (2026-06-11). Lower is better; raising any
# number needs an explicit review decision.
FX_BASELINE = {
    # api.c: 2 raw FX constants used as attack-id table KEYS for spacie-article items.
    "api.c": 2,
    # damage_terminal_owner.h: kind-keyed (hoisted kind switch).
    "damage_terminal_owner.h": 0,
    # items.c: kind-keyed (shine-reflect enters via the reverse kind map).
    "items.c": 0,
    # The special machines are kind-keyed end to end (compares via
    # msl_motion_state_fx_special_kind, writes via msl_motion_state_action_for_fx_kind,
    # submotions via the owners table).
    "locomotion.c": 0,
    "shine.c": 0,
    "blaster.c": 0,
}

SPACIE_GATE_BASELINE = {
    # Machine ADMISSION is now data-driven (char_owns_*_machine = the owners table maps
    # the machine's defining kind; locomotion/shine/blaster carry NO char-id lists). The
    # remaining gates are replay-validated rollout owner scoping in shared combat code.
    "api.c": 1,
    "combat.c": 12,
    "items.c": 0,
    "items_spacies.c": 1,
    "locomotion.c": 0,
}


def _counts(pattern: str) -> dict[str, int]:
    rx = re.compile(pattern)
    out: dict[str, int] = {}
    for f in sorted((ROOT / "src").iterdir()):
        if f.suffix not in (".c", ".h") or f.name in EXEMPT:
            continue
        text = f.read_text(encoding="utf-8", errors="replace")
        if f.name == "action_ids.h":
            text = "\n".join(
                line for line in text.splitlines() if not _ENUMERATOR_DEF_RE.match(line)
            )
        n = len(rx.findall(text))
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
