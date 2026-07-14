from __future__ import annotations

import re
import struct
import sys
from pathlib import Path

import numpy as np
import pytest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))

from test_char_common_action_coverage import _mk_inputs, _run, _seed_base  # noqa: E402


def _read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8")


def _function_body(source: str, name: str) -> str:
    match = re.search(
        rf"\b(?:static\s+)?(?:inline\s+)?(?:int|void|float|uint8_t)\s+{re.escape(name)}\s*\([^)]*\)\s*\{{",
        source,
    )
    assert match is not None, f"missing function {name}"
    start = match.end()
    depth = 1
    pos = start
    while pos < len(source) and depth:
        ch = source[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
        pos += 1
    assert depth == 0, f"unterminated function {name}"
    return source[start : pos - 1]


def _assert_ordered(text: str, needles: list[str]) -> None:
    cursor = -1
    for needle in needles:
        pos = text.find(needle, cursor + 1)
        assert pos >= 0, f"missing {needle!r}"
        assert pos > cursor, f"{needle!r} appears out of order"
        cursor = pos


def _laser_record_by_char(char_id_target: int) -> tuple[int, int, int, int, int, int, int, int]:
    path = ROOT / "data/items/lasers.bin"
    if not path.exists():
        pytest.skip(f"missing local artifact: {path.relative_to(ROOT)}")
    buf = path.read_bytes()
    assert buf[:8] == b"MSLLASR1"
    (version,) = struct.unpack_from("<I", buf, 8)
    assert version in (6, 7, 8, 9, 10)
    (count,) = struct.unpack_from("<H", buf, 12)
    off = 16
    record_bytes = 682 if version >= 10 else 546 if version >= 9 else 514 if version >= 8 else 226 if version >= 7 else 218
    for _ in range(int(count)):
        base = off
        char_id = int(buf[base])
        shot_itkind = int(struct.unpack_from("<H", buf, base + 2)[0])
        gun_itkind = int(struct.unpack_from("<H", buf, base + 4)[0])
        spawn_bone = int(struct.unpack_from("<H", buf, base + 6)[0])
        lifetime = int(struct.unpack_from("<H", buf, base + 40)[0])
        state0_kbg = int(struct.unpack_from("<H", buf, base + 42 + 10)[0])
        state0_wsk = int(struct.unpack_from("<H", buf, base + 42 + 12)[0])
        state0_bkb = int(struct.unpack_from("<H", buf, base + 42 + 14)[0])
        state0_zero_kb = int(buf[base + 42 + 18])
        if char_id == char_id_target:
            return (
                shot_itkind,
                gun_itkind,
                spawn_bone,
                lifetime,
                state0_kbg,
                state0_wsk,
                state0_bkb,
                state0_zero_kb,
            )
        off += record_bytes
    raise AssertionError(f"missing MSLLASR1 char_id={char_id_target}")


def test_projectiles_reflect_source_completion_doc_is_closed() -> None:
    doc = _read("agent_docs/systems/projectiles_reflect.md")
    progress = _read("agent_docs/systems/PROGRESS.md")

    assert "| Projectiles / Reflect | CLOSED | [projectiles_reflect.md](projectiles_reflect.md)" in progress
    assert "| TODO" not in doc
    assert "| INVENTORY NEEDED |" not in doc
    assert "| OPEN |" not in doc
    assert "| BLOCKED |" not in doc
    assert "This document has no BLOCKED rows" in doc
    for token in (
        "MSLLASR1",
        "MSLITAR1",
        "MSLFTSC1",
        "itFoxlaser_UnkMotion1_Anim",
        "Item_80269F14",
        "Item_80269DC8",
        "ftColl_80077464",
        "zero-KB",
        "shield-bounce",
        "item hitlists",
    ):
        assert token in doc


def test_msllasr1_laser_data_closes_fox_zero_kb_and_falco_kb_boundary() -> None:
    fox = _laser_record_by_char(1)
    falco = _laser_record_by_char(22)

    assert fox[0] != falco[0]  # shot item kind
    assert fox[1] != falco[1]  # gun item kind
    assert fox[2] == falco[2] == 49  # ftFx_SpecialN hold-joint part id
    assert fox[3] > 0 and falco[3] > 0
    assert fox[4:7] == (0, 0, 0)
    assert fox[7] == 1
    assert falco[4:7] != (0, 0, 0)
    assert falco[7] == 0


def test_laser_shield_bounce_uses_source_segment_threshold_not_old_hemisphere_proxy() -> None:
    items = _read("src/items_spacies.c")
    assert re.search(r"\blaser_try_shield_bounce_velocity\s*\(", items) is None

    body = _function_body(items, "laser_try_shield_bounce_velocity_from_segment")
    assert "lbColl_800077A0" in body
    assert "Item_80269DC8" in body
    assert "shield_bounce_threshold_radians" in body
    assert "acosf" in body
    assert "ny > fabsf(nx)" not in body


def test_laser_runtime_keeps_source_order_for_spawn_collision_and_post_callbacks() -> None:
    source = _read("src/items_laser.c")
    body = _function_body(source, "lasers_source_update_and_collide")

    _assert_ordered(
        body,
        [
            "msl_item_reflect_apply_pending_laser_callback(batch, ii)",
            "frame->prev_x = batch->state.item_pos_x[ii]",
            "batch->state.item_pos_x[ii] = frame->x",
            "stage_collision_item_line_hits_runtime(",
            "laser_try_fighter_contact(",
        ],
    )
    contact = _function_body(source, "laser_try_fighter_contact")
    assert "item_hitcapsule_select_fighter_contact(" in contact


def test_throw_laser_birth_frame_marker_does_not_suppress_generic_body_contact() -> None:
    # Runtime behavior proof for the Throw-side spawn marker: it prevents double-applying spawn
    # motion when needed, but the ordinary item GObj callback can still run BODY contact in the same
    # frame. No hidden callback/provenance lane is set here; the 2% Fox laser BODY hit is generic
    # item-vs-fighter collision.
    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    # refs/melee/src/melee/it/items/itfoxlaser.c::{
    #   it_8029C6CC,itFoxlaser_UnkMotion1_Phys,itFoxlaser_UnkMotion1_Coll}
    shot_itkind, _, _, lifetime, *_ = _laser_record_by_char(1)
    seed = _seed_base("fox")
    seed["pos_x"][0, 1] = np.float32(10.0)
    seed["pos_y"][0, 1] = np.float32(0.0)
    item = seed["items"][0, 0]
    item["exists"] = np.uint8(1)
    item["state"] = np.uint8(1)
    item["type"] = np.uint16(shot_itkind)
    item["owner"] = np.int8(0)
    item["instance_id"] = np.uint16(123)
    item["attack_id"] = np.uint16(shot_itkind)
    item["attack_instance"] = np.uint16(1)
    item["direction"] = np.float32(1.0)
    item["vel_x"] = np.float32(0.0)
    item["vel_y"] = np.float32(0.0)
    item["pos_x"] = np.float32(10.0)
    item["pos_y"] = np.float32(8.0)
    item["timer"] = np.float32(lifetime)
    item["spawn_id"] = np.uint32(1)
    seed["item_hidden_callback_flags"][0, 0] = np.uint8(1 << 1)  # spawned_this_frame only

    out = _run(seed, [_mk_inputs()])[0]
    assert float(out["percent"][1]) == pytest.approx(2.0, abs=0.01)
    assert int(out["items"]["exists"][0]) == 0


def test_projectile_reflect_comments_do_not_claim_proxy_closure() -> None:
    items = _read("src/items_spacies.c")
    lasers = _read("src/items_laser.c")
    checked = "\n".join(
        [
            _read("agent_docs/systems/projectiles_reflect.md"),
            _read("src/item_reflect.h"),
            _read("src/laser_params.h"),
            _read("tools/extraction/extract_lasers.py"),
            _function_body(items, "blaster_gun_run_owner_callback"),
            _function_body(items, "laser_spawn_from_fighter"),
            _function_body(items, "laser_try_shield_bounce_velocity_from_segment"),
            _function_body(lasers, "lasers_source_update_and_collide"),
            _function_body(items, "items_spawn_fighter_anim_callback"),
        ]
    )

    for stale in (
        "KB-triplet-derived proxy",
        "zero-KB-authoritative-signal",
        "narrowed_temporary",
        "Narrow approximation",
        "best available proxy",
        "not yet seed",
        "promoted into seed/runtime state",
        "approximated with pose matrices",
        "cmd1_cur proxies",
        "proxy fighter-collision",
        "identity-cap proxy",
    ):
        assert stale not in checked
