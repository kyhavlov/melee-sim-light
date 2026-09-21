"""Capture eligibility for the strict replay gate (not mismatch classification)."""
from __future__ import annotations

import hashlib
import json
import math
import struct
from functools import lru_cache
from pathlib import Path

import numpy as np
import pyarrow as pa


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "agent_docs/validation/replay_admission.json"
RAW_AXES = {"raw_analog_x", "raw_analog_y", "raw_analog_cstick_x", "raw_analog_cstick_y"}
PROCESSED_INPUTS = {"joystick", "cstick", "triggers", "buttons", "random_seed"}
PHYSICAL_INPUTS = {"buttons_physical", "triggers_physical"}
# Online/Core/BrawlOffscreenDamage.asm: identical gameplay instructions, with
# either the old 176-byte or current 224-byte register-save stack frame. Exclude
# only the relocated final branch, whose instruction form is checked below.
OFFSCREEN_BODIES = {
    "b000b6d61152440d954376a726377eb1411dd1fc0e117e325fbebeed96b2accf",
    "620a47b16ef11f9ac3e844ad7df1a7eece6d10526629c4e4f88f262cdc66f16c",
}


@lru_cache(maxsize=1)
def evidence() -> dict:
    return json.loads(EVIDENCE.read_text())


def gecko_codes(raw: bytes) -> dict[int, bytes]:
    """Read direct and split Gecko events from the decoded SLP event stream."""
    if raw[:11] != b"{U\x03raw[$U#l" or raw[15] != 0x35:
        raise ValueError("unsupported SLP event envelope")
    end = 15 + int.from_bytes(raw[11:15], "big")
    count = raw[16]
    sizes = {raw[i]: int.from_bytes(raw[i + 1:i + 3], "big") for i in range(17, 16 + count, 3)}
    position = 16 + count
    chunks = []
    while position < end:
        command = raw[position]
        size = sizes[command]
        payload = raw[position + 1:position + 1 + size]
        if command == 0x10 and payload[-2] == 0x3D:
            chunks.append(payload[:int.from_bytes(payload[-4:-2], "big")])
        elif command == 0x3D:
            chunks.append(payload)
        position += size + 1
    if position != end or end > len(raw):
        raise ValueError("truncated SLP event stream")
    codes = b"".join(chunks)
    result = {}
    position = 0
    while position < len(codes):
        address, value = struct.unpack_from(">II", codes, position)
        kind = (address >> 24) & 0xFE
        size = (8 + 8 * value if kind in (0xC0, 0xC2) else
                16 if kind == 8 else
                8 + (value + 7) // 8 * 8 if kind == 6 else 8)
        if position + size > len(codes):
            raise ValueError("truncated Gecko block")
        result[address] = codes[position:position + size]
        position += size
    return result


def stadium_is_frozen(start: dict, codes: dict[int, bytes]) -> bool:
    # Before the toggle patch, online Slippi bypassed transformation logic
    # at 0x801d4578 while SendGameInfo still reported the console FSToggle.
    # Online/Core/PreventFileAlarms/FreezeStadium.asm (ccb59238).
    patch = codes.get(0xC21D4578, b"")
    legacy_frozen = (len(patch) == 32 and patch[8:-4] == bytes.fromhex(
        "ffe008903d80801d618c4fd87d8903a64e800420") and
        int.from_bytes(patch[-4:], "big") & 0xFC000003 == 0x48000000)
    return start.get("is_frozen_ps") is True or legacy_frozen


def capture_issues(game, raw: bytes, *, played_on: str | None = None,
                   fnmsubs_profile: str | None = None) -> list[str]:
    """Return property violations without running or consulting simulator output."""
    issues = []
    identity = hashlib.sha256(raw).hexdigest()
    record = next((r for r in evidence()["excluded"] if r["sha256_slp"] == identity), None)
    if record is not None:
        issues.extend(record["reasons"])
    codes = gecko_codes(raw)
    offscreen = codes.get(0xC206A880)
    if offscreen is None:
        issues.append("vanilla-magnifier")
    elif (len(offscreen) != 224 or
          hashlib.sha256(offscreen[8:-4]).hexdigest() not in OFFSCREEN_BODIES or
          int.from_bytes(offscreen[-4:], "big") & 0xFC000003 != 0x48000000):
        issues.append("unreviewed-offscreen-patch")

    start = game.start
    if (start.get("stage") not in {2, 3, 8, 28, 31, 32} or
        start.get("is_pal") is not False or
        start.get("is_raining_bombs") is not False or
        start.get("item_spawn_frequency") != -1 or
        not math.isfinite(start.get("damage_ratio", float("nan"))) or
        start.get("damage_ratio", 0) <= 0 or start.get("timer") != 480 or
        start.get("bitfield") not in ([50, 1, 134, 76], [50, 1, 142, 76])):
        issues.append("unsupported-settings")
    if start.get("stage") == 3 and not stadium_is_frozen(start, codes):
        issues.append("unfrozen-stadium")
    players = start["players"]
    if len(players) not in (2, 3, 4) or (len(players) > 2 and not start.get("is_teams")):
        issues.append("unsupported-settings")
    # build_match_config carries one shared stock count, individual handicaps
    # and match_damage_ratio. These supported values need not equal defaults.
    if len({p.get("stocks") for p in players}) != 1:
        issues.append("unsupported-settings")
    for player in players:
        if (player.get("character") not in range(26) or
            player.get("type") not in ("Human", "Cpu") or
            player.get("stocks") not in range(1, 256) or player.get("handicap") not in range(1, 10) or
            player.get("damage_start") != 0 or player.get("damage_spawn") != 0 or
            any(player.get(key) != 1 for key in ("offense_ratio", "defense_ratio", "model_scale")) or
            player.get("ucf") != {"dash_back": "Ucf", "shield_drop": "Ucf"}):
            issues.append("unsupported-settings")
        if player.get("type") == "Cpu" and player.get("cpu_level") not in range(1, 10):
            issues.append("missing-cpu-level")
    platform = played_on or game.metadata.get("playedOn")
    if platform not in ("dolphin", "mainline dolphin", "nintendont", "network"):
        issues.append("unestablished-arithmetic")
    if fnmsubs_profile is not None:
        profiles = json.loads((ROOT / "agent_docs/validation/arithmetic_provenance.json").read_text())
        if not any(r["sha256_slp"] == identity and r["fnmsubs_profile"] == fnmsubs_profile
                   for r in profiles["captures"]):
            issues.append("unestablished-arithmetic")

    ids = game.frames.field("id").to_numpy(zero_copy_only=False)
    _, reversed_rows = np.unique(ids[::-1], return_index=True)
    rows = np.sort(len(ids) - 1 - reversed_rows)
    frames = ids[rows]
    if (len(frames) < 2 or frames[0] != -123 or np.any(np.diff(frames) != 1) or
        game.end is None or (game.metadata.get("lastFrame") is not None and
                            game.metadata["lastFrame"] != int(frames[-1]))):
        issues.append("incomplete-history")
    ports = game.frames.field("ports")
    for player in players:
        port = player["port"]
        if port not in ports.type.names:
            issues.append("incomplete-history")
            continue
        entity = ports.field(port)
        for role in entity.type.names:
            fighter = entity.field(role)
            pre, post = fighter.field("pre"), fighter.field("post")
            valid = (fighter.is_valid().to_numpy(zero_copy_only=False) &
                     post.is_valid().to_numpy(zero_copy_only=False) &
                     post.field("character").is_valid().to_numpy(zero_copy_only=False))
            present = np.flatnonzero(valid)
            if not len(present):
                if role == "leader":
                    issues.append("incomplete-history")
                continue
            # Match native.c::build_finalized_rows: each player takes its last
            # valid row for an ID, which may precede that ID's final global row.
            _, last = np.unique(ids[present][::-1], return_index=True)
            player_rows = np.sort(present[len(present) - 1 - last])
            if role == "leader":
                required = PROCESSED_INPUTS | (RAW_AXES | PHYSICAL_INPUTS if player["type"] == "Human" else set())
                if required - set(pre.type.names):
                    issues.append("missing-recorded-inputs")
                if not np.all(pre.is_valid().to_numpy(zero_copy_only=False)[player_rows]):
                    issues.append("missing-recorded-inputs")
                for name in required & set(pre.type.names):
                    field = pre.field(name)
                    columns = [field]
                    if pa.types.is_struct(field.type):
                        columns += [field.field(i) for i in range(field.type.num_fields)]
                    if any(not np.all(c.is_valid().to_numpy(zero_copy_only=False)[player_rows])
                           for c in columns):
                        issues.append("missing-recorded-inputs")
            characters = post.field("character").to_numpy(zero_copy_only=False)[player_rows]
            if np.any((characters < 0) | (characters > 26)):
                issues.append("unsupported-settings")
            # ExtendPlayerBlock allocates 0x2600 bytes; InitPlayerData clears
            # only the original fighter extent. A sleeping transform partner
            # has not run SlippiResetLCancelStatus at its first publication.
            if 7 in characters and 19 in characters:
                issues.append("uninitialized-transform-lcancel")
    items = game.frames.field("item").take(pa.array(rows)).values
    kinds = items.field("type").to_numpy(zero_copy_only=False)
    states = items.field("state").to_numpy(zero_copy_only=False)
    # itseakchain.c: x18 is first assigned by it_802BC080 after the initial
    # picked-up states. Do not condition this rule on the sampled misc3 value.
    if np.any((kinds == 97) & (states < 3)):
        issues.append("uninitialized-chain")
    return sorted(set(issues))


def require_admissible(game, path: Path, *, played_on: str | None = None,
                       fnmsubs_profile: str | None = None) -> None:
    issues = capture_issues(game, path.read_bytes(), played_on=played_on,
                            fnmsubs_profile=fnmsubs_profile)
    if issues:
        raise ValueError("ineligible capture: " + ", ".join(issues))
