from __future__ import annotations

import importlib
import json
from pathlib import Path
from typing import Any

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE
from tools.modelplay.sim_env import build_match_config_array

FIXTURE = "tests/fixtures/modelplay/manual_webplay_combat_repros.json"

MSL_COLLIDE_RIGHT_WALL_PUSH = 0x00000040
MSL_COLLIDE_RIGHT_WALL_HUG = 0x00000800


def _collision_contacts_dtype() -> np.dtype:
    return np.dtype(
        [
            ("wall_kind", ("u1", (4,))),
            ("_pad0", ("u1", (4,))),
            ("wall_id", ("<u2", (4,))),
            ("wall_contact_x", ("<f4", (4,))),
            ("wall_contact_y", ("<f4", (4,))),
            ("wall_normal_x", ("<f4", (4,))),
            ("wall_normal_y", ("<f4", (4,))),
            ("ceiling_id", ("<u2", (4,))),
            ("_pad1", ("<u2", (4,))),
            ("ceiling_contact_x", ("<f4", (4,))),
            ("ceiling_contact_y", ("<f4", (4,))),
            ("ceiling_normal_x", ("<f4", (4,))),
            ("ceiling_normal_y", ("<f4", (4,))),
            ("coll_env_flags", ("<u4", (4,))),
            ("coll_prev_env_flags", ("<u4", (4,))),
            ("damage_hitlag_wall_asdi_latch", ("u1", (4,))),
        ],
        align=False,
    )


def _root() -> Path:
    return Path(__file__).resolve().parents[1]


def _load_fixture() -> dict[str, Any]:
    fixture = json.loads((_root() / FIXTURE).read_text(encoding="utf-8"))
    assert fixture["input_fields"] == ["buttons", "mainX", "mainY", "cX", "cY", "l", "r"]
    assert [trace["name"] for trace in fixture["traces"]] == [
        "charged_upsmash",
        "falco_knocked_against_stage",
        "drill_reset",
        "reverse_shine_in_knockdown",
    ]
    return fixture


def _trace_by_name(name: str) -> dict[str, Any]:
    for trace in _load_fixture()["traces"]:
        if trace["name"] == name:
            return trace
    raise AssertionError(f"missing trace fixture {name!r}")


def _stick_i8(v: float) -> np.int8:
    return np.int8(np.clip(np.rint(float(v) * 80.0), -80, 80))


def _write_input_player(arr: np.ndarray, player: int, values: list[float | int]) -> None:
    buttons, main_x, main_y, c_x, c_y, l_trigger, r_trigger = values
    arr["p"][0, player]["buttons"] = np.uint16(int(buttons))
    arr["p"][0, player]["main_x"] = _stick_i8(float(main_x))
    arr["p"][0, player]["main_y"] = _stick_i8(float(main_y))
    arr["p"][0, player]["c_x"] = _stick_i8(float(c_x))
    arr["p"][0, player]["c_y"] = _stick_i8(float(c_y))
    arr["p"][0, player]["l"] = np.uint8(np.clip(np.rint(float(l_trigger) * 255.0), 0, 255))
    arr["p"][0, player]["r"] = np.uint8(np.clip(np.rint(float(r_trigger) * 255.0), 0, 255))


def _inputs_by_frame(ranges: list[list[Any]]) -> dict[int, list[float | int]]:
    out: dict[int, list[float | int]] = {}
    for start, end, values in ranges:
        for frame in range(int(start), int(end) + 1):
            out[frame] = values
    return out


def _replay_trace(
    trace: dict[str, Any], *, end_frame: int | None = None, include_contacts: bool = False
) -> dict[int, np.void] | dict[int, tuple[np.void, np.void]]:
    binding = importlib.import_module("msl_binding")
    sizes = binding.sizes()
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    contacts_dtype = _collision_contacts_dtype()
    contacts_stride = int(sizes["collision_contacts"])
    assert int(contacts_dtype.itemsize) == contacts_stride

    players = sorted(trace["players"], key=lambda p: int(p["slot"]))
    config = build_match_config_array(
        num_players=2,
        char_ids=tuple(int(p["internal_char_id"]) for p in players),
        team_ids=tuple(int(p["team_id"]) for p in players),
        facing=tuple(int(p["facing"]) for p in players),
        stage_id=int(trace["stage_id"]),
        frame_id=0,
        random_seed=int(trace["seed"]),
    )
    p1_inputs = _inputs_by_frame(trace["p1_input_ranges"])
    p2_inputs = _inputs_by_frame(trace["p2_input_ranges"])

    out_compare = np.zeros((1, compare_stride), dtype=np.uint8)
    out_contacts = np.zeros((1, contacts_stride), dtype=np.uint8)
    prev_input = np.zeros((1, input_stride), dtype=np.uint8)
    history: dict[int, np.void] | dict[int, tuple[np.void, np.void]] = {}
    final_frame = int(end_frame if end_frame is not None else trace["end_frame"])

    handle = binding.init(batch_size=1, num_players=2)
    try:
        binding.init_match(handle, config.view(np.uint8).reshape((1, -1)))
        binding.write_compare(handle, out_compare)
        out = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()
        if include_contacts:
            binding.debug_write_collision_contacts(handle, out_contacts)
            contacts = out_contacts.view(contacts_dtype).reshape(-1)[0].copy()
            history[0] = (out, contacts)
        else:
            history[0] = out
        for frame_i in range(0, final_frame):
            input_t = np.zeros((1,), dtype=INPUT_DTYPE)
            _write_input_player(input_t, 0, p1_inputs.get(frame_i + 1, [0, 0, 0, 0, 0, 0, 0]))
            _write_input_player(input_t, 1, p2_inputs.get(frame_i + 1, [0, 0, 0, 0, 0, 0, 0]))
            input_bytes = input_t.view(np.uint8).reshape((1, input_stride))
            binding.step_input(handle, prev_input, input_bytes)
            binding.write_compare(handle, out_compare)
            out = out_compare.view(COMPARE_DTYPE).reshape(-1)[0].copy()
            if include_contacts:
                binding.debug_write_collision_contacts(handle, out_contacts)
                contacts = out_contacts.view(contacts_dtype).reshape(-1)[0].copy()
                history[frame_i + 1] = (out, contacts)
            else:
                history[frame_i + 1] = out
            prev_input = input_bytes.copy()
    finally:
        binding.destroy(handle)

    return history


@pytest.mark.integration
def test_manual_charged_upsmash_releases_scaled_damage() -> None:
    # Source owner:
    # - ftCo_800DEF38/ftCo_800DF0D0 track smash hold and release.
    # - ftAction_80073008 extracts the start-smash-charge damage scalar from the action script.
    # - ftColl_8007ABD0 applies the held-frame scalar to the released HitCapsule damage.
    # refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEF38,ftCo_800DF0D0}
    # refs/melee/src/melee/ft/ftaction.c::ftAction_80073008
    # refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0
    history = _replay_trace(_trace_by_name("charged_upsmash"), end_frame=398)

    falco = 1
    assert float(history[390]["percent"][falco]) == pytest.approx(28.14, abs=1e-4)
    assert int(history[397]["action_id"][falco]) == 90  # DamageFlyTop.
    # The retained smash-release owner uses the source `ftAction_804D82A0` single-precision
    # 0.003906 literal rather than the rounded 1/256 approximation previously assumed here.
    assert float(history[397]["percent"][falco]) == pytest.approx(52.747799, abs=1e-4)
    assert int(history[397]["hitstun"][falco]) >= 67


@pytest.mark.integration
def test_manual_damagefly_wall_hit_enters_flyreflectwall() -> None:
    # Source owner: DamageFly_Coll calls wall tech first, then ftCo_800C17CC for no-tech
    # FlyReflectWall/FlyReflectCeil. The repro hits Falco against FD's right wall while no wall tech
    # is available; he must bounce into FlyReflectWall instead of sliding through the wall into
    # DownBound.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::{
    #   ftCo_800C15F4,ftCo_800C17CC,ftCo_800C18A8}
    history = _replay_trace(
        _trace_by_name("falco_knocked_against_stage"), end_frame=840, include_contacts=True
    )

    falco = 1
    out_821, contacts_821 = history[821]
    out_822, contacts_822 = history[822]
    out_823, contacts_823 = history[823]
    out_840, _ = history[840]
    assert int(out_821["action_id"][falco]) == 88  # DamageFlyN before wall reflect.
    assert int(contacts_821["wall_kind"][falco]) == 0

    assert int(out_822["action_id"][falco]) == 247  # FlyReflectWall.
    assert int(out_822["animation_index"][falco]) == 212  # WallDamage.
    # Keep the decomp-shaped projection honest: FlyReflect is entered from the right-wall hug
    # contact, and the root has already been projected to the post-collision position before the
    # reflected velocity is applied.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::ftCo_800C18A8
    assert float(out_822["pos_x"][falco]) == pytest.approx(88.11599, abs=1e-4)
    assert float(out_822["pos_y"][falco]) == pytest.approx(-14.13644, abs=1e-4)
    assert int(contacts_822["wall_kind"][falco]) == 2
    assert int(contacts_822["wall_id"][falco]) == 9
    assert float(contacts_822["wall_contact_x"][falco]) == pytest.approx(85.56570, abs=1e-4)
    assert float(contacts_822["wall_contact_y"][falco]) == pytest.approx(-7.80649, abs=1e-4)
    assert float(contacts_822["wall_normal_x"][falco]) == pytest.approx(1.0, abs=1e-6)
    assert float(contacts_822["wall_normal_y"][falco]) == pytest.approx(0.0, abs=1e-6)
    assert int(contacts_822["coll_env_flags"][falco]) & MSL_COLLIDE_RIGHT_WALL_PUSH
    assert int(contacts_822["coll_env_flags"][falco]) & MSL_COLLIDE_RIGHT_WALL_HUG
    assert float(out_822["speed_x_attack"][falco]) > 1.0
    assert int(out_822["hurtbox_state"][falco]) == 2

    assert int(out_823["action_id"][falco]) == 247
    assert float(out_823["pos_x"][falco]) == pytest.approx(90.10555, abs=1e-4)
    assert float(out_823["pos_y"][falco]) == pytest.approx(-12.45038, abs=1e-4)
    assert int(contacts_823["wall_kind"][falco]) == 2
    assert int(out_840["action_id"][falco]) == 247
    assert int(out_840["hurtbox_state"][falco]) == 0


@pytest.mark.integration
def test_manual_drill_reset_uses_remaining_downdamage_timer() -> None:
    # Source owner: DownDamage_Anim enters DownWait through ftCo_80097F38 when the hidden
    # downdamage x0 timer remains positive. That path preserves the remaining timer instead of
    # reinitializing the full DownWait timer, so weak downed drill hits produce a jab-reset getup.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Anim
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
    #   ftCo_80097F38,ftCo_DownWait_IASA}
    history = _replay_trace(_trace_by_name("drill_reset"), end_frame=329)

    falco = 1
    assert int(history[278]["action_id"][falco]) == 185  # DownDamageU.
    assert int(history[294]["action_id"][falco]) == 184  # Short DownWaitU.
    assert int(history[298]["action_id"][falco]) == 186  # DownStandU getup.
    assert int(history[329]["action_id"][falco]) == 14  # Wait, not long knockdown wait.


@pytest.mark.integration
def test_manual_reverse_shine_on_knockdown_uses_collision_facing_for_kb() -> None:
    # Source owner: ftCo_8009F184 preserves the downed victim's visible facing but
    # ftCo_8008DCE0's velocity path uses the collision-owned damage facing lane for knockback.
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_8009F184
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    history = _replay_trace(_trace_by_name("reverse_shine_in_knockdown"), end_frame=270)

    falco = 1
    assert int(history[260]["action_id"][falco]) == 184  # DownWaitU before shine.
    assert int(history[264]["action_id"][falco]) == 185  # DownDamageU.
    assert int(history[264]["facing"][falco]) == 0
    assert float(history[264]["speed_x_attack"][falco]) < -2.0
    assert float(history[270]["pos_x"][falco]) < float(history[260]["pos_x"][falco])
