from __future__ import annotations

import argparse
import struct
from dataclasses import dataclass
from pathlib import Path

from melee_sim.hsd_archive import parse_hsd_archive

# Reuse decomp-first Pl*.dat parsers.
from tools.extraction.extract_character_attrs import _extract_ftco_dattrs, _extract_fox_falco_laser


def _f32(x: float) -> float:
    # Keep literals as Python floats but ensure they round to f32 when packed.
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


@dataclass(frozen=True)
class LaserRecord:
    char_id: int
    shot_itkind: int
    gun_itkind: int
    spawn_bone_part_id: int
    spawn_off: tuple[float, float, float]
    ground_start_msid: int
    ground_loop_msid: int
    ground_end_msid: int
    air_start_msid: int
    air_loop_msid: int
    air_end_msid: int
    blaster_angle: float
    blaster_speed: float
    lifetime_frames: int
    laser_damage: float
    laser_size: float
    laser_angle: int
    laser_kbg: int
    laser_wsk: int
    laser_bkb: int
    laser_element: int
    laser_shield_damage: int
    laser_zero_kb_damage_class: int
    hitbox_x138_mask: int
    hitbox_offsets_x: tuple[float, ...]
    laser_damage_update_frame: int
    laser_damage_update_hitbox_mask: int
    laser_damage_update_damage: float
    laser_state1_damage: float
    laser_state1_size: float
    laser_state1_angle: int
    laser_state1_kbg: int
    laser_state1_wsk: int
    laser_state1_bkb: int
    laser_state1_element: int
    laser_state1_shield_damage: int
    laser_state1_zero_kb_damage_class: int
    state1_hitbox_x138_mask: int
    state1_hitbox_offsets_x: tuple[float, ...]


def _load_record(*, iso_dir: Path, dat_name: str, ftdata_symbol: str, char_id: int) -> LaserRecord:
    pl_path = iso_dir / dat_name
    buf = pl_path.read_bytes()
    arc = parse_hsd_archive(buf)

    ftdata_abs = arc.get_public_offset(ftdata_symbol)
    if ftdata_abs is None:
        raise SystemExit(f"{dat_name}: missing public symbol {ftdata_symbol!r}")

    # Pull blaster dat attrs and laser article attrs/hitbox from Pl*.dat.
    co = _extract_ftco_dattrs(pl_path, ftdata_symbol=ftdata_symbol, extract_fox_blaster=True)
    laser = _extract_fox_falco_laser(buf, arc, ftdata_abs=ftdata_abs)

    # SpecialN submotion ids are GALE01 game-code enums (not DAT attrs). Keep them table-driven
    # (stored in lasers.bin) and cite decomp source-of-truth:
    # - refs/melee/src/melee/ft/chara/ftFox/forward.h::ftFx_Submotion
    ground_start_msid = 295
    ground_loop_msid = 296
    ground_end_msid = 297
    air_start_msid = 298
    air_loop_msid = 299
    air_end_msid = 300

    # Spawn bone and local offset are decomp-defined (not stored in DAT attrs).
    # Decomp: refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_FtGetHoldJoint
    spawn_bone_part_id = 49  # FtPart_RThumbNb (refs/melee/src/melee/ft/forward.h)
    spawn_off = (_f32(0.0), _f32(1.2325000762939453), _f32(4.263599872589111))

    def _derive_zero_kb_damage_class(kbg: int, wsk: int, bkb: int) -> int:
        # Source owner:
        # - it_2725.c writes article script kbg/wsk/bkb into HitCapsule.x24/x28/x2C.
        # - ftColl_80077C60 item BODY contact writes percent-temp/applied damage.
        # - ftcoll.c KB helpers consume the HitCapsule KB tuple, and
        #   Fighter_ProcessHit_8006D1EC enters Damage* only when applied KB is nonzero.
        # For supported Fox/Falco blaster article states, the all-zero KB tuple is therefore the
        # data-backed percent-only/no-Damage-entry class. Falco laser states carry nonzero KB terms
        # and must stay on the normal flinching item BODY path.
        return 1 if (int(kbg) == 0 and int(wsk) == 0 and int(bkb) == 0) else 0

    hitbox_offsets_x = tuple(
        float(x) for x in (laser.get("laser_hitbox_offsets_x") or []) if isinstance(x, (int, float))
    )
    hitbox_offsets_x_state1 = tuple(
        float(x)
        for x in (laser.get("laser_state1_hitbox_offsets_x") or [])
        if isinstance(x, (int, float))
    )

    return LaserRecord(
        char_id=int(char_id),
        shot_itkind=int(co.get("blaster_shot_itkind", 0)),
        gun_itkind=int(co.get("blaster_gun_itkind", 0)),
        spawn_bone_part_id=int(spawn_bone_part_id),
        spawn_off=spawn_off,
        ground_start_msid=int(ground_start_msid),
        ground_loop_msid=int(ground_loop_msid),
        ground_end_msid=int(ground_end_msid),
        air_start_msid=int(air_start_msid),
        air_loop_msid=int(air_loop_msid),
        air_end_msid=int(air_end_msid),
        blaster_angle=float(co.get("blaster_angle", 0.0)),
        blaster_speed=float(co.get("blaster_vel", 0.0)),
        lifetime_frames=int(laser.get("laser_lifetime_frames", 0)),
        laser_damage=float(laser.get("laser_damage", 0.0)),
        laser_size=float(laser.get("laser_size", 0.0)),
        laser_angle=int(laser.get("laser_angle", 0)),
        laser_kbg=int(laser.get("laser_kbg", 0)),
        laser_wsk=int(laser.get("laser_wsk", 0)),
        laser_bkb=int(laser.get("laser_bkb", 0)),
        laser_element=int(laser.get("laser_element", 0)),
        laser_shield_damage=int(laser.get("laser_shield_damage", 0)),
        laser_zero_kb_damage_class=_derive_zero_kb_damage_class(
            int(laser.get("laser_kbg", 0)),
            int(laser.get("laser_wsk", 0)),
            int(laser.get("laser_bkb", 0)),
        ),
        hitbox_x138_mask=int(laser.get("laser_hitbox_x138_mask", 0)),
        hitbox_offsets_x=hitbox_offsets_x,
        laser_damage_update_frame=int(laser.get("laser_damage_update_frame", 0)),
        laser_damage_update_hitbox_mask=int(laser.get("laser_damage_update_hitbox_mask", 0)),
        laser_damage_update_damage=float(laser.get("laser_damage_update_damage", 0.0)),
        laser_state1_damage=float(laser.get("laser_state1_damage", laser.get("laser_damage", 0.0))),
        laser_state1_size=float(laser.get("laser_state1_size", laser.get("laser_size", 0.0))),
        laser_state1_angle=int(laser.get("laser_state1_angle", laser.get("laser_angle", 0))),
        laser_state1_kbg=int(laser.get("laser_state1_kbg", laser.get("laser_kbg", 0))),
        laser_state1_wsk=int(laser.get("laser_state1_wsk", laser.get("laser_wsk", 0))),
        laser_state1_bkb=int(laser.get("laser_state1_bkb", laser.get("laser_bkb", 0))),
        laser_state1_element=int(laser.get("laser_state1_element", laser.get("laser_element", 0))),
        laser_state1_shield_damage=int(
            laser.get("laser_state1_shield_damage", laser.get("laser_shield_damage", 0))
        ),
        laser_state1_zero_kb_damage_class=_derive_zero_kb_damage_class(
            int(laser.get("laser_state1_kbg", laser.get("laser_kbg", 0))),
            int(laser.get("laser_state1_wsk", laser.get("laser_wsk", 0))),
            int(laser.get("laser_state1_bkb", laser.get("laser_bkb", 0))),
        ),
        state1_hitbox_x138_mask=int(
            laser.get("laser_state1_hitbox_x138_mask", laser.get("laser_hitbox_x138_mask", 0))
        ),
        state1_hitbox_offsets_x=hitbox_offsets_x_state1 if hitbox_offsets_x_state1 else hitbox_offsets_x,
    )


def _pack_record(rec: LaserRecord) -> bytes:
    # Fixed-capacity payload for hot-path use (no variable-length allocations at runtime).
    MAX_HITBOX_OFFS = 16

    offs0 = list(rec.hitbox_offsets_x)[:MAX_HITBOX_OFFS]
    offs0 += [0.0] * (MAX_HITBOX_OFFS - len(offs0))
    offs1 = list(rec.state1_hitbox_offsets_x)[:MAX_HITBOX_OFFS]
    offs1 += [0.0] * (MAX_HITBOX_OFFS - len(offs1))

    # Layout is documented in agent_docs/DATA_CONTRACT.md (MSLLASR1 v7).
    out = bytearray()
    out += struct.pack(
        "<BBHHHHHHHHHff3fH",
        int(rec.char_id) & 0xFF,
        0,
        int(rec.shot_itkind) & 0xFFFF,
        int(rec.gun_itkind) & 0xFFFF,
        int(rec.spawn_bone_part_id) & 0xFFFF,
        int(rec.ground_start_msid) & 0xFFFF,
        int(rec.ground_loop_msid) & 0xFFFF,
        int(rec.ground_end_msid) & 0xFFFF,
        int(rec.air_start_msid) & 0xFFFF,
        int(rec.air_loop_msid) & 0xFFFF,
        int(rec.air_end_msid) & 0xFFFF,
        _f32(rec.blaster_angle),
        _f32(rec.blaster_speed),
        _f32(rec.spawn_off[0]),
        _f32(rec.spawn_off[1]),
        _f32(rec.spawn_off[2]),
        int(rec.lifetime_frames) & 0xFFFF,
    )
    sd = int(rec.laser_shield_damage)
    if sd < -128:
        sd = -128
    if sd > 127:
        sd = 127
    out += struct.pack(
        "<ffHHHHbBBBH2x",
        _f32(rec.laser_damage),
        _f32(rec.laser_size),
        int(rec.laser_angle) & 0xFFFF,
        int(rec.laser_kbg) & 0xFFFF,
        int(rec.laser_wsk) & 0xFFFF,
        int(rec.laser_bkb) & 0xFFFF,
        sd,
        int(rec.laser_element) & 0xFF,
        int(rec.laser_zero_kb_damage_class) & 0xFF,
        min(len(rec.hitbox_offsets_x), MAX_HITBOX_OFFS) & 0xFF,
        int(rec.hitbox_x138_mask) & 0xFFFF,
    )
    out += struct.pack("<" + "f" * MAX_HITBOX_OFFS, *[_f32(x) for x in offs0])
    out += struct.pack(
        "<fHBB",
        _f32(rec.laser_damage_update_damage),
        int(rec.laser_damage_update_hitbox_mask) & 0xFFFF,
        int(rec.laser_damage_update_frame) & 0xFF,
        0,
    )

    sd1 = int(rec.laser_state1_shield_damage)
    if sd1 < -128:
        sd1 = -128
    if sd1 > 127:
        sd1 = 127
    out += struct.pack(
        "<ffHHHHbBBBH2x",
        _f32(rec.laser_state1_damage),
        _f32(rec.laser_state1_size),
        int(rec.laser_state1_angle) & 0xFFFF,
        int(rec.laser_state1_kbg) & 0xFFFF,
        int(rec.laser_state1_wsk) & 0xFFFF,
        int(rec.laser_state1_bkb) & 0xFFFF,
        sd1,
        int(rec.laser_state1_element) & 0xFF,
        int(rec.laser_state1_zero_kb_damage_class) & 0xFF,
        min(len(rec.state1_hitbox_offsets_x), MAX_HITBOX_OFFS) & 0xFF,
        int(rec.state1_hitbox_x138_mask) & 0xFFFF,
    )
    out += struct.pack("<" + "f" * MAX_HITBOX_OFFS, *[_f32(x) for x in offs1])
    return bytes(out)


def main() -> None:
    ap = argparse.ArgumentParser(description="Extract Fox/Falco blaster laser params into a compact .bin (decomp-first).")
    ap.add_argument("--iso_dir", type=Path, default=Path("_iso"), help="directory containing extracted Pl*.dat files")
    ap.add_argument("--out", type=Path, default=Path("data/items/lasers.bin"))
    args = ap.parse_args()

    recs = [
        _load_record(iso_dir=args.iso_dir, dat_name="PlFx.dat", ftdata_symbol="ftDataFox", char_id=1),
        _load_record(iso_dir=args.iso_dir, dat_name="PlFc.dat", ftdata_symbol="ftDataFalco", char_id=22),
    ]

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("wb") as f:
        f.write(b"MSLLASR1")
        f.write(struct.pack("<I", 7))
        f.write(struct.pack("<H", len(recs)))
        f.write(struct.pack("<H", 0))
        for r in recs:
            f.write(_pack_record(r))


if __name__ == "__main__":
    main()
