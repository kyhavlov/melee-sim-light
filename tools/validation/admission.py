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

from tools.validation.slpz import RAW_HEADER, _event_sizes


ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / "agent_docs/validation/replay_admission.json"
CSTICK_AXES = {"raw_analog_cstick_x", "raw_analog_cstick_y"}
RAW_AXES = {"raw_analog_x", "raw_analog_y"} | CSTICK_AXES
PROCESSED_INPUTS = {"joystick", "cstick", "triggers", "buttons", "random_seed"}
PHYSICAL_INPUTS = {"buttons_physical", "triggers_physical"}
REPLAY_PLATFORMS = {"dolphin", "mainline dolphin", "nintendont", "network"}
# Online/Core/BrawlOffscreenDamage.asm: identical gameplay instructions, with
# either the old 176-byte or current 224-byte register-save stack frame. Exclude
# only the relocated final branch, whose instruction form is checked below.
OFFSCREEN_BODIES = {
    "b000b6d61152440d954376a726377eb1411dd1fc0e117e325fbebeed96b2accf",
    "620a47b16ef11f9ac3e844ad7df1a7eece6d10526629c4e4f88f262cdc66f16c",
}


# External/PreventWobbling: four-hit singles limit and CaptureWait resets.
# Hash instruction bodies only; the Gecko return branch is relocated.
PREVENT_WOBBLING_BODIES = {
    0xC208F090: (496, "d5382be0df327bcfc7e379903064a02fd12a9b42c0d6c94917c3f1ef4df080c5"),
    0xC20DB880: (32, "db2d14ce9fed80a7a34ac9fb911097d3e6b5a624fb2c58df662accea793da60f"),
    0xC20DBBD4: (32, "db2d14ce9fed80a7a34ac9fb911097d3e6b5a624fb2c58df662accea793da60f"),
}

# Slippi ASM 9eff55f Output/Netplay/GALE01r2.ini, before 6d3e140.
# These UCF 0.8 mechanics consume raw main-stick X and processed axes.
UCF_08_BODIES = {
    0xC20C9A44: (352, "b7df39bd809882969cfd2b464a4ae0018bf9a20e0738ef420edb17dc7ff02e0e"),
    0xC20998A4: (312, "f12ab1dfb51574be5bbc5e5ef7aba4e313386e94de2a3ee697eb29402224ab2e"),
    0xC20908F4: (192, "366450d63b99b8a454fb1c4d41ba5930af017d6c74681fcec01971f173e1697a"),
}
UCF_084_HOOKS = {0xC206B460, 0xC208E54C, 0xC2093294, 0xC209A0B8, 0xC20D65EC}


def is_ucf_08(codes: dict[int, bytes]) -> bool:
    if UCF_084_HOOKS.intersection(codes):
        return False
    for address, (size, digest) in UCF_08_BODIES.items():
        patch = codes.get(address, b"")
        if (len(patch) != size or hashlib.sha256(patch[8:-4]).hexdigest() != digest or
                int.from_bytes(patch[-4:], "big") & 0xFC000003 != 0x48000000):
            return False
    return True


def has_ucf_084_cardinals(codes: dict[int, bytes]) -> bool:
    # Slippi ASM 6d3e140, UCF Pad Buffer + 1.0 Cardinals. The recorded block
    # contains 48 bytes of mutable pad history after the first branch.
    patch = codes.get(0xC206B460, b"")
    return (len(patch) == 680 and
            hashlib.sha256(patch[8:12] + patch[60:-4]).hexdigest() ==
            "f1aa8cbb0b44918a5e59bdddfc233a11f206a9400c94092918ca937d0b62ea74" and
            int.from_bytes(patch[-4:], "big") & 0xFC000003 == 0x48000000)


@lru_cache(maxsize=1)
def evidence() -> dict:
    return json.loads(EVIDENCE.read_text())


def gecko_codes(raw: bytes) -> dict[int, bytes]:
    """Read direct and split Gecko events from the decoded SLP event stream."""
    return replay_events(raw)[0]


def replay_events(raw: bytes) -> tuple[dict[int, bytes], tuple[int, int] | None, list[int]]:
    """Read Gecko codes, the terminal bookend and Game End event offsets."""
    if len(raw) < 17 or raw[:11] != RAW_HEADER:
        raise ValueError("unsupported SLP event envelope")
    end = 15 + int.from_bytes(raw[11:15], "big")
    if end > len(raw):
        raise ValueError("truncated SLP event stream")
    sizes, count = _event_sizes(raw[15:min(end, 273)])
    position = 17 + 3 * count
    chunks = []
    last_bookend_end = 0
    endings = []
    while position < end:
        command = raw[position]
        size = sizes[command]
        if not size:
            raise ValueError(f"unexpected Slippi event 0x{command:02x} at byte {position}")
        next_position = position + 1 + size
        if next_position > end:
            raise ValueError("truncated SLP event stream")
        if command == 0x10:
            payload = raw[position + 1:next_position]
            if len(payload) >= 4 and payload[-2] == 0x3D:
                chunks.append(payload[:int.from_bytes(payload[-4:-2], "big")])
        elif command == 0x3D:
            chunks.append(raw[position + 1:next_position])
        elif command == 0x3C and size >= 8:
            last_bookend_end = next_position
        elif command == 0x39:
            endings.append(position)
        position = next_position
    tail = end
    for offset in reversed(endings):
        if offset + 1 + sizes[0x39] != tail:
            break
        tail = offset
    bookend = (struct.unpack_from(">ii", raw, tail - sizes[0x3C])
               if last_bookend_end == tail else None)
    codes = b"".join(chunks)
    if len(codes) % 8:
        raise ValueError("truncated Gecko block")
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
    return result, bookend, endings


def stadium_is_frozen(start: dict, codes: dict[int, bytes]) -> bool:
    # Before the toggle patch, online Slippi bypassed transformation logic
    # at 0x801d4578 while SendGameInfo still reported the console FSToggle.
    # Online/Core/PreventFileAlarms/FreezeStadium.asm (ccb59238).
    patch = codes.get(0xC21D4578, b"")
    legacy_frozen = (len(patch) == 32 and patch[8:-4] == bytes.fromhex(
        "ffe008903d80801d618c4fd87d8903a64e800420") and
        int.from_bytes(patch[-4:], "big") & 0xFC000003 == 0x48000000)
    return start.get("is_frozen_ps") is True or legacy_frozen


# wire.h MSL_PATCH_*.
PATCH_FD_BACKGROUND_FROZEN = 1 << 0
PATCH_ALLOW_WOBBLING = 1 << 1
PATCH_RETAIL_SPAWNS = 1 << 2
PATCH_FAST_FALL_EFFECT = 1 << 3
PATCH_RESET_INTERRUPT = 1 << 5
PATCH_L_CANCEL_FLASH = 1 << 6
PATCH_STADIUM_MONITOR = 1 << 7
PATCH_UCF_08_SPOTDODGE = 1 << 8

STADIUM_MONITOR_PATCH = {
    0x041D2AC0: bytes.fromhex("041d2ac038a00000"),
    0x041D2AC8: bytes.fromhex("041d2ac838800000"),
    0x041D2B98: bytes.fromhex("041d2b9848000018"),
}


def whispy_patch(codes: dict[int, bytes]) -> int:
    """Return -1 if no Gecko list is available, otherwise the recorded setting."""
    if not codes:
        return -1
    patch = codes.get(0xC208653C)
    if patch is None:
        return 0
    # Online/Core/WhispyBlowDirFix at 0x8008653c also affects item spawn
    # direction through ftLib_800864A8.
    if (len(patch) != 56 or hashlib.sha256(patch[8:-4]).hexdigest() !=
            "af4349210f7c59046e021bda981813dd3d5ef641ec6fc9d3afd76cb63b6cc79c" or
            int.from_bytes(patch[-4:], "big") & 0xFC000003 != 0x48000000):
        raise ValueError("unsupported-whispy-direction-patch")
    return 1


def slippi_patches(codes: dict[int, bytes]) -> int:
    """Identify supported Slippi patches from their recorded instructions."""
    patches = 0
    spotdodge = codes.get(0xC20998A4, b"")
    # The 240-byte hook keeps the 0.8 float rim test with the newer UCF bundle.
    if (len(spotdodge) == 248 and hashlib.sha256(spotdodge[8:-4]).hexdigest() ==
            "1315df92c5a5b48e9fd424f75d1b7a32d559c728f9481c7802cc5484ff726ceb" and
            int.from_bytes(spotdodge[-4:], "big") & 0xFC000003 == 0x48000000):
        patches |= PATCH_UCF_08_SPOTDODGE
    if codes.get(0x0421AAE4, b"")[4:8] == bytes.fromhex("60000000"):
        patches |= PATCH_FD_BACKGROUND_FROZEN
    if 0xC20DA9DC in codes:  # Earlier three-hit version.
        raise ValueError("unsupported-prevent-wobbling-patch")
    if any(address in codes for address in PREVENT_WOBBLING_BODIES):
        for address, (size, digest) in PREVENT_WOBBLING_BODIES.items():
            patch = codes.get(address, b"")
            if (len(patch) != size or
                    hashlib.sha256(patch[8:-4]).hexdigest() != digest or
                    int.from_bytes(patch[-4:], "big") & 0xFC000003 != 0x48000000):
                raise ValueError("unsupported-prevent-wobbling-patch")
    elif codes:
        patches |= PATCH_ALLOW_WOBBLING
    if codes and 0xC216E510 not in codes:
        patches |= PATCH_RETAIL_SPAWNS
    patch = codes.get(0xC207D5A0)
    if patch is not None:
        if (len(patch) != 120 or hashlib.sha256(patch[8:-4]).hexdigest() !=
                "f4cdcc3652437230294e27613dc690913bf3615a869db1058758cc54d2678446" or
                int.from_bytes(patch[-4:], "big") & 0xFC000003 != 0x48000000):
            raise ValueError("unsupported-fast-fall-effect-patch")
        patches |= PATCH_FAST_FALL_EFFECT
    patch = codes.get(0xC2069774)
    if patch is not None:
        if (len(patch) != 32 or patch[8:-4] != bytes.fromhex(
                "981a2218881a22183880000050803e30981a2218") or
                int.from_bytes(patch[-4:], "big") & 0xFC000003 != 0x48000000):
            raise ValueError("unsupported-interrupt-reset-patch")
        patches |= PATCH_RESET_INTERRUPT
    patch = codes.get(0xC208D698, b"")
    if (len(patch) == 72 and patch[8:-4] == bytes.fromhex(
            "7c0500004080001cc06600e8ec611824fc60181ed861002880e1002c48000010"
            "fc60081ed861002880e1002c8103002c90e823407c05000060000000") and
            int.from_bytes(patch[-4:], "big") & 0xFC000003 == 0x48000000):
        patches |= PATCH_L_CANCEL_FLASH
    if any(address in codes for address in STADIUM_MONITOR_PATCH):
        if not all(codes.get(address) == patch for address, patch in STADIUM_MONITOR_PATCH.items()):
            raise ValueError("unsupported-stadium-monitor-patch")
        patches |= PATCH_STADIUM_MONITOR
    return patches


def capture_issues(game, raw: bytes, *, played_on: str | None = None,
                   fnmsubs_profile: str | None = None,
                   jit_arithmetic: str | None = None,
                   codes: dict[int, bytes] | None = None,
                   allow_ucf_08: bool = False,
                   allow_missing_cstick: bool = False,
                   allow_missing_game_end: bool = False,
                   finalized_through: int | None = None) -> list[str]:
    """Return property violations without running or consulting simulator output."""
    issues = []
    identity = hashlib.sha256(raw).hexdigest()
    record = next((r for r in evidence()["excluded"] if r["sha256_slp"] == identity), None)
    if codes is None:
        codes = gecko_codes(raw)
    legacy_ucf = allow_ucf_08 and is_ucf_08(codes)
    if record is not None:
        # Recheck these against the requested UCF profile and frame range.
        recheck = set()
        if legacy_ucf or allow_missing_cstick:
            recheck.add("missing-recorded-inputs")
        if allow_missing_game_end and game.end is None:
            recheck.add("incomplete-history")
        issues.extend(reason for reason in record["reasons"] if reason not in recheck)
    raw_axes = {"raw_analog_x"} if legacy_ucf else RAW_AXES
    try:
        slippi_patches(codes)
    except ValueError as exc:
        issues.append(str(exc))
    offscreen = codes.get(0xC206A880)
    if offscreen is None:
        issues.append("vanilla-magnifier")
    elif (len(offscreen) != 224 or
          hashlib.sha256(offscreen[8:-4]).hexdigest() not in OFFSCREEN_BODIES or
          int.from_bytes(offscreen[-4:], "big") & 0xFC000003 != 0x48000000):
        issues.append("unreviewed-offscreen-patch")

    start = game.start
    timer = start.get("timer")
    if (start.get("stage") not in {2, 3, 8, 28, 31, 32} or
        start.get("is_pal") is not False or
        start.get("is_raining_bombs") is not False or
        start.get("item_spawn_frequency") != -1 or
        not math.isfinite(start.get("damage_ratio", float("nan"))) or
        start.get("damage_ratio", 0) <= 0 or
        start.get("bitfield") not in ([50, 1, 134, 76], [50, 1, 142, 76])):
        issues.append("unsupported-settings")
    if start.get("stage") == 3 and not stadium_is_frozen(start, codes):
        issues.append("unfrozen-stadium")
    players = start["players"]
    if len(players) not in (2, 3, 4) or (len(players) > 2 and not start.get("is_teams")):
        issues.append("unsupported-settings")
    for player in players:
        if (player.get("character") not in range(26) or
            player.get("type") not in ("Human", "Cpu") or
            player.get("stocks") not in range(1, 256) or player.get("handicap") not in range(1, 10) or
            player.get("damage_start") not in range(256) or player.get("damage_spawn") != 0 or
            any(player.get(key) != 1 for key in ("offense_ratio", "defense_ratio", "model_scale")) or
            player.get("ucf") != {"dash_back": "Ucf", "shield_drop": "Ucf"}):
            issues.append("unsupported-settings")
        if player.get("type") == "Cpu" and player.get("cpu_level") not in range(1, 10):
            issues.append("missing-cpu-level")
    platform = played_on or game.metadata.get("playedOn")
    if platform not in REPLAY_PLATFORMS:
        issues.append("unestablished-arithmetic")
    if fnmsubs_profile is not None:
        profiles = json.loads((ROOT / "agent_docs/validation/arithmetic_provenance.json").read_text())
        if not any(r["sha256_slp"] == identity and r.get("fnmsubs_profile") == fnmsubs_profile
                   for r in profiles["captures"]):
            issues.append("unestablished-arithmetic")
    # The recording does not encode its double-FMA semantics. An override
    # needs reviewed arithmetic evidence, not an inferred CPU identity.
    if jit_arithmetic not in (None, "fma"):
        profiles = json.loads((ROOT / "agent_docs/validation/arithmetic_provenance.json").read_text())
        if not any(r["sha256_slp"] == identity and r.get("jit_arithmetic") == jit_arithmetic
                   for r in profiles["captures"]):
            issues.append("unestablished-arithmetic")

    ids = game.frames.field("id").to_numpy(zero_copy_only=False)
    _, reversed_rows = np.unique(ids[::-1], return_index=True)
    rows = np.sort(len(ids) - 1 - reversed_rows)
    frames = ids[rows]
    last_recorded = int(frames.max()) if len(frames) else None
    if finalized_through is not None:
        frames = frames[frames <= finalized_through]
    if (len(frames) < 2 or frames[0] != -123 or np.any(np.diff(frames) != 1) or
        (game.end is None and not allow_missing_game_end) or
        game.metadata.get("lastFrame") not in (None, last_recorded, finalized_through) or
        (finalized_through is not None and frames[-1] != finalized_through)):
        issues.append("incomplete-history")
    if timer != 480:
        end = game.end or {}
        method = end.get("method")
        quit_port = end.get("lras_initiator")
        if timer not in range(1, 2**32) or len(frames) < 2:
            issues.append("unsupported-settings")
        else:
            end_frame = int(frames[-1])
            ended_early = (end_frame - int(frames[0]) < 60 * timer and
                           (method == "Game" or (method == "NoContest" and
                            any(p["port"] == quit_port for p in players))))
            # Online NTSC singles records expiry at the configured countdown
            # in 60 Hz frames. See gmvs.c::fn_8016CD98 and gm_GetMatchOutcome.
            timed_out = (method == "Time" and end_frame == 60 * timer and
                         start.get("scene", {}).get("major") == 8 and
                         len(players) == 2 and not start.get("is_teams"))
            prefix = (allow_missing_game_end and game.end is None and end_frame < 60 * timer)
            if not (ended_early or timed_out or prefix):
                issues.append("unsupported-settings")
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
            if finalized_through is not None:
                valid &= ids <= finalized_through
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
                required = PROCESSED_INPUTS | (raw_axes | PHYSICAL_INPUTS if player["type"] == "Human" else set())
                if allow_missing_cstick and CSTICK_AXES.isdisjoint(pre.type.names):
                    required -= CSTICK_AXES
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
    return sorted(set(issues))


def require_admissible(game, path: Path, *, played_on: str | None = None,
                       fnmsubs_profile: str | None = None,
                       jit_arithmetic: str | None = None) -> None:
    issues = capture_issues(game, path.read_bytes(), played_on=played_on,
                            fnmsubs_profile=fnmsubs_profile,
                            jit_arithmetic=jit_arithmetic)
    if issues:
        raise ValueError("ineligible capture: " + ", ".join(issues))
