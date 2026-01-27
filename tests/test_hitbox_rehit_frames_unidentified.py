from __future__ import annotations

import struct
from pathlib import Path


def _iter_u16_7_from_mslhitb1(path: Path):
    buf = path.read_bytes()
    if buf[:8] != b"MSLHITB1":
        raise AssertionError(f"{path}: bad magic {buf[:8]!r}")
    (ver,) = struct.unpack_from("<I", buf, 8)
    if int(ver) != 1:
        raise AssertionError(f"{path}: unsupported version {ver} (want 1)")
    (entry_count,) = struct.unpack_from("<I", buf, 12)

    idx_base = 16
    idx_bytes = 12
    rec_bytes = 44

    for i in range(int(entry_count)):
        off = idx_base + i * idx_bytes
        msid, rec_count, rec_len, payload_off = struct.unpack_from("<HHII", buf, off)
        if int(rec_len) != int(rec_count) * rec_bytes:
            raise AssertionError(f"{path}: bad rec_len for msid={msid}: {rec_len} != {rec_count}*{rec_bytes}")
        for ri in range(int(rec_count)):
            roff = int(payload_off) + ri * rec_bytes
            (u16_7,) = struct.unpack_from("<H", buf, roff + 42)
            yield int(u16_7)


def test_fighter_rehit_frames_all_zero_until_writer_identified():
    """
    Guardrail: fighters' `HitCapsule.x40_b4` writer is not yet identified, so extracted MSLHITB1
    `u16_7` must keep `rehit_frames` == 0 (indefinite) for Fox/Falco.

    Decomp/ASM trail:
    - Hitlist timer uses `HitCapsule.x40_b4`:
      refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 and ::lbColl_80008A5C
    - Fighter create-hitbox does NOT write `HitCapsule.x40_b4` (bits 4..11 at hitbox+0x40):
      refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_8007121C
    - As of this investigation, no GALE01 fighter-side writer has been found for the bits 20..27 insert
      used to update `HitCapsule.x40_b4` (the `rlwimi ...,4,20,27` pattern).
    - A confirmed writer exists on the item side:
      refs/melee/build/GALE01/asm/melee/it/it_2725.s::it_802790C0
      (at 0x802793CC..0x802793D8: lhz 0x40; rlwimi ...,4,20,27; sth 0x40)
    """
    for ch in ["fox", "falco"]:
        path = Path("data") / "hitboxes" / f"{ch}.bin"
        u16_7 = list(_iter_u16_7_from_mslhitb1(path))
        assert u16_7, f"{path}: expected at least one record"
        rehit = sorted({x & 0xFF for x in u16_7})
        assert rehit == [0], (
            f"{path}: unexpected nonzero rehit_frames {rehit}. "
            "Do not reintroduce JSON patching/guessing; first identify the fighter-side writer for HitCapsule.x40_b4."
        )
