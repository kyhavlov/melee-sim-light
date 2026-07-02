from __future__ import annotations

from pathlib import Path

import pytest

from tools.slippi.action_state_tables import read_mslacid1_v3
from tools.slippi.suite_io import load_suite
from tests.replay_buffers_loader import load_replay_buffers


@pytest.mark.integration
def test_suite_observed_actions_do_not_require_ft_800895E0_rewrite_paths() -> None:
    """
    Guard against unmodeled ft_800895E0 rewrite paths.

    ft_800895E0 uses (u8)MotionState.x4_flags as a compare key against fp+0x2073, but has two
    special-case rewrites that replace the x2070 word before writing fp->x2070:
    - If fp->kind == 0x11 and flags_low == 0x71, it overwrites the word with 0x240063.
    - If flags_low == 0x62 and it_8026B6C8(fp->x1974) is true, it overwrites the word with 0x44003D.
    refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0

    The simulator currently does not model the required fighter-kind/item-pointer state for these
    rewrites, so enforce that suite-observed action_ids for Fox/Falco never use x4_flags low bytes
    0x71 or 0x62.
    """

    suite_path = Path("replays/suites/fox_falco_fd_ucf084_recent.json")
    if not suite_path.exists():
        pytest.skip("missing suite manifest: replays/suites/fox_falco_fd_ucf084_recent.json")
    suite = load_suite(suite_path)
    replay_labels = sorted(Path(entry.replay) for entry in suite.replays)

    fox_low = read_mslacid1_v3(Path("data/attack_id/move_id/fox.bin")).x4_flags_low
    falco_low = read_mslacid1_v3(Path("data/attack_id/move_id/falco.bin")).x4_flags_low

    bad: set[tuple[int, int, int]] = set()

    for p in replay_labels:
        ds = load_replay_buffers(str(p))
        s = ds.rows
        for field in ("seed_t", "ref_t1"):
            action = s[field]["action_id"][:, :2]
            char = s[field]["char_id"][:, :2]

            for i in range(action.shape[0]):
                for pl in range(2):
                    cid = int(char[i, pl])
                    aid = int(action[i, pl])
                    if cid == 1:
                        lb = int(fox_low[aid]) if 0 <= aid < int(fox_low.shape[0]) else 0
                    elif cid == 22:
                        lb = int(falco_low[aid]) if 0 <= aid < int(falco_low.shape[0]) else 0
                    else:
                        continue
                    if lb in (0x71, 0x62):
                        bad.add((cid, aid, lb))

    if bad:
        msg = ", ".join(
            [f"(char={cid}, action_id={aid}, flags_low=0x{lb:02x})" for (cid, aid, lb) in sorted(bad)]
        )
        raise AssertionError(
            "suite contains action_ids with MotionState.x4_flags low byte requiring ft_800895E0 rewrite paths: "
            + msg
        )
