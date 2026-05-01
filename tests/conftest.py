from __future__ import annotations

import subprocess
import sys
import warnings
from pathlib import Path

# Make the repo root importable so `tools.*` modules can be imported in tests.
ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


def _ensure_ecb_bottom_tables() -> None:
    for ch in ("fox", "falco"):
        out = ROOT / "data" / "ecb" / f"{ch}_bottom.bin"
        if out.exists():
            try:
                with out.open("rb") as f:
                    magic = f.read(8)
                    ver = int.from_bytes(f.read(4), "little", signed=False)
                    _anim_count = int.from_bytes(f.read(2), "little", signed=False)
                    stride = int.from_bytes(f.read(2), "little", signed=False)  # reserved in v2
                if magic == b"MSLECB01" and ver == 3:
                    # Bottom tables use reserved=0 for historical compatibility.
                    if stride == 0:
                        continue
            except OSError:
                pass
        anims = ROOT / "data" / "anims" / f"{ch}.bin"
        attrs = ROOT / "data" / "characters" / f"{ch}.json"
        if not anims.exists():
            raise RuntimeError(f"missing required anims file for tests: {anims}")
        if not attrs.exists():
            raise RuntimeError(f"missing required attrs file for tests: {attrs}")
        subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.extraction.extract_ecb_bottom",
                "--character",
                ch,
                "--anims",
                str(anims),
                "--attrs",
                str(attrs),
                "--out",
                str(out),
            ],
            check=True,
        )


def _ensure_ecb_extents_tables() -> None:
    for ch in ("fox", "falco"):
        out = ROOT / "data" / "ecb" / f"{ch}_extents.bin"
        if out.exists():
            try:
                with out.open("rb") as f:
                    magic = f.read(8)
                    ver = int.from_bytes(f.read(4), "little", signed=False)
                    _anim_count = int.from_bytes(f.read(2), "little", signed=False)
                    stride = int.from_bytes(f.read(2), "little", signed=False)
                if magic == b"MSLECB01" and ver == 4 and stride == 16:
                    continue
            except OSError:
                pass
        anims = ROOT / "data" / "anims" / f"{ch}.bin"
        attrs = ROOT / "data" / "characters" / f"{ch}.json"
        if not anims.exists():
            raise RuntimeError(f"missing required anims file for tests: {anims}")
        if not attrs.exists():
            raise RuntimeError(f"missing required attrs file for tests: {attrs}")
        subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.extraction.extract_ecb_extents",
                "--character",
                ch,
                "--anims",
                str(anims),
                "--attrs",
                str(attrs),
                "--out",
                str(out),
            ],
            check=True,
        )


def _tracks_missing_msids(path: Path, want: set[int]) -> set[int]:
    import struct

    with path.open("rb") as f:
        magic = f.read(8)
        if magic != b"SSANIMT1":
            raise ValueError(f"bad tracks magic: {magic!r}")
        (version,) = struct.unpack("<I", f.read(4))
        if version != 3:
            raise ValueError(f"unsupported tracks version: {version}")
        local_count, anim_count = struct.unpack("<HH", f.read(4))
        f.read(local_count)  # local_parts
        f.read(2 * local_count)  # local_parent
        f.read(4 * local_count)  # local_flags

        missing = set(want)
        for _ in range(anim_count):
            (msid,) = struct.unpack("<H", f.read(2))
            f.read(4)  # end_frame
            f.read(1)  # aobj_loop
            f.read(1)  # uses_root_motion
            missing.discard(int(msid))
            for _lp in range(local_count):
                part_u8 = f.read(1)
                if not part_u8:
                    raise ValueError("unexpected EOF in tracks parts")
                (n_tracks,) = struct.unpack("<B", f.read(1))
                for _t in range(n_tracks):
                    hdr = f.read(8)
                    if len(hdr) != 8:
                        raise ValueError("unexpected EOF in tracks header")
                    (_obj_type, _frac_value, _frac_slope, _pad, _startframe, length) = struct.unpack(
                        "<BBBBHH", hdr
                    )
                    f.read(int(length))
            if not missing:
                return set()
        return missing


def _ensure_tracks_bins() -> None:
    # Some tests depend on SSANIMT1 end_frame values (e.g. landing anim-rate scaling). These
    # artifacts are generated from local `_iso/` extracts and may be gitignored.
    need_msids = {36, 73, 74, 75, 76, 77}  # LandingFallSpecial + LandingAir*
    for ch, prefix in (("fox", "PlFx"), ("falco", "PlFc")):
        tracks = ROOT / "data" / "anims" / f"{ch}.tracks.bin"
        if tracks.exists():
            try:
                if not _tracks_missing_msids(tracks, need_msids):
                    continue
            except OSError:
                pass

        required = [
            ROOT / "_iso" / "PlCo.dat",
            ROOT / "_iso" / f"{prefix}.dat",
            ROOT / "_iso" / f"{prefix}Nr.dat",
            ROOT / "_iso" / f"{prefix}AJ.dat",
        ]
        missing_iso = [p for p in required if not p.exists()]
        if missing_iso:
            raise RuntimeError(
                f"missing required tracks file for tests: {tracks} (and cannot rebuild due to missing _iso/ files: {missing_iso}). "
                f"Run: `uv run python -m tools.extraction.build_data --iso-dir _iso --stage grnla --chars fox,falco`"
            )

        subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.extraction.extract_fighter_anims",
                "--character",
                ch,
                "--out-dir",
                "data/anims",
            ],
            check=True,
        )

        if not tracks.exists():
            raise RuntimeError(f"failed to generate required tracks file for tests: {tracks}")
        missing = sorted(_tracks_missing_msids(tracks, need_msids))
        if missing:
            raise RuntimeError(f"tracks file missing required msids for tests: {tracks} missing={missing}")


def _dyn_contract_ok(path: Path, want_collision_msids: set[int]) -> bool:
    try:
        with path.open("rb") as f:
            magic = f.read(8)
            version = int.from_bytes(f.read(4), "little", signed=False)
            set_count = int.from_bytes(f.read(2), "little", signed=False)
            total_nodes = int.from_bytes(f.read(2), "little", signed=False)
            if magic != b"SSDYNN01" or version != 4:
                return False
            for _set_i in range(set_count):
                f.read(2)  # root_part
                node_count = int.from_bytes(f.read(2), "little", signed=False)
                f.read(12)  # pos
                f.read(64 * node_count)
            count = int.from_bytes(f.read(2), "little", signed=False)
            f.read(2)  # reserved
            got = {int.from_bytes(f.read(2), "little", signed=False) for _ in range(count)}
            return got == want_collision_msids and total_nodes <= 4
    except OSError:
        return False


def _ensure_dyn_bins() -> None:
    expected = {
        "fox": {17, 58},
        "falco": set(),
    }
    stale = False
    for ch, want in expected.items():
        if not _dyn_contract_ok(ROOT / "data" / "anims" / f"{ch}.dyn.bin", want):
            stale = True
            break
    if not stale:
        return

    required = [
        ROOT / "_iso" / "PlCo.dat",
        ROOT / "_iso" / "PlFx.dat",
        ROOT / "_iso" / "PlFxNr.dat",
        ROOT / "_iso" / "PlFxAJ.dat",
        ROOT / "_iso" / "PlFc.dat",
        ROOT / "_iso" / "PlFcNr.dat",
        ROOT / "_iso" / "PlFcAJ.dat",
    ]
    missing_iso = [p for p in required if not p.exists()]
    if missing_iso:
        warnings.warn(
            "missing/stale optional dynamic pose artifact(s), and cannot rebuild because _iso inputs "
            "are missing: "
            f"{missing_iso}. Dynamic-pose-specific tests should skip unless these artifacts are "
            "available. Run: `uv run python -m tools.extraction.build_data --iso-dir _iso --stage "
            "grnla --chars fox,falco`",
            RuntimeWarning,
        )
        return
    subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.extraction.build_data",
            "--iso-dir",
            str(ROOT / "_iso"),
            "--stage",
            "grnla",
            "--chars",
            "fox,falco",
        ],
        check=True,
    )
    for ch, want in expected.items():
        path = ROOT / "data" / "anims" / f"{ch}.dyn.bin"
        if not _dyn_contract_ok(path, want):
            raise RuntimeError(f"failed to generate required dynamic pose artifact: {path}")


def _ensure_hurtcaps_bins() -> None:
    for ch in ("fox", "falco"):
        out = ROOT / "data" / "hurtcaps" / f"{ch}.bin"
        if out.exists():
            try:
                with out.open("rb") as f:
                    magic = f.read(8)
                    ver = int.from_bytes(f.read(4), "little", signed=False)
                if magic == b"MSLHURT1" and ver == 1:
                    continue
            except OSError:
                pass

        # Rebuild from local `_iso/` extracts (fast; no full ISO rebuild).
        required = [
            ROOT / "_iso" / "PlCo.dat",
            ROOT / "_iso" / ("PlFx.dat" if ch == "fox" else "PlFc.dat"),
        ]
        missing_iso = [p for p in required if not p.exists()]
        if missing_iso:
            raise RuntimeError(
                f"missing required hurtcaps bin for tests: {out} (and cannot rebuild due to missing _iso/ files: {missing_iso}). "
                f"Run: `uv run python -m tools.extraction.build_data --iso-dir _iso --stage grnla --chars fox,falco`"
            )

        subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.extraction.extract_fighter_hurtcapsules",
                "--iso_dir",
                str(ROOT / "_iso"),
                "--out_dir",
                str(ROOT / "data" / "hurtcaps"),
                "--out_bin_dir",
                str(ROOT / "data" / "hurtcaps"),
                "--character",
                ch,
            ],
            check=True,
        )

        if not out.exists():
            raise RuntimeError(f"failed to generate required hurtcaps bin for tests: {out}")

def _ensure_hurtbox_states_bins() -> None:
    for ch in ("fox", "falco"):
        out = ROOT / "data" / "hurtbox_states" / f"{ch}.bin"
        if out.exists():
            try:
                with out.open("rb") as f:
                    magic = f.read(8)
                    ver = int.from_bytes(f.read(4), "little", signed=False)
                if magic == b"MSLHURM1" and ver == 1:
                    continue
            except OSError:
                pass

        # Rebuild from local `_iso/` extracts (fast; no full ISO rebuild).
        required = [
            ROOT / "_iso" / "PlCo.dat",
            ROOT / "_iso" / ("PlFx.dat" if ch == "fox" else "PlFc.dat"),
        ]
        missing_iso = [p for p in required if not p.exists()]
        if missing_iso:
            raise RuntimeError(
                f"missing required hurtbox states bin for tests: {out} (and cannot rebuild due to missing _iso/ files: {missing_iso}). "
                f"Run: `uv run python -m tools.extraction.build_data --iso-dir _iso --stage grnla --chars fox,falco`"
            )

        # `extract_fighter_hurtbox_modes` depends on existing ISO-derived hurtcaps and special_msids.
        hurtcaps = ROOT / "data" / "hurtcaps" / f"{ch}.bin"
        msids = ROOT / "data" / "special_msids" / f"{ch}.json"
        if not hurtcaps.exists():
            raise RuntimeError(f"missing required hurtcaps bin for hurtbox states rebuild: {hurtcaps}")
        if not msids.exists():
            # Build from local `_iso/` extracts (fast; no full ISO rebuild).
            subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "tools.extraction.extract_special_msids",
                    "--iso_dir",
                    str(ROOT / "_iso"),
                    "--out_dir",
                    str(ROOT / "data" / "special_msids"),
                    "--chars",
                    ch,
                ],
                check=True,
            )
        if not msids.exists():
            raise RuntimeError(f"failed to generate required special_msids json for hurtbox states rebuild: {msids}")

        subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.extraction.extract_fighter_hurtbox_modes",
                "--iso_dir",
                str(ROOT / "_iso"),
                "--melee_decomp",
                str(ROOT / "refs" / "melee"),
                "--hurtcaps_dir",
                str(ROOT / "data" / "hurtcaps"),
                "--special_msids_dir",
                str(ROOT / "data" / "special_msids"),
                "--out_dir",
                str(ROOT / "data" / "hurtbox_states"),
                "--chars",
                ch,
            ],
            check=True,
        )

        if not out.exists():
            raise RuntimeError(f"failed to generate required hurtbox states bin for tests: {out}")

def _ensure_hit_status_bins() -> None:
    for ch in ("fox", "falco"):
        out = ROOT / "data" / "hit_status" / f"{ch}.bin"
        if out.exists():
            try:
                with out.open("rb") as f:
                    magic = f.read(8)
                    ver = int.from_bytes(f.read(4), "little", signed=False)
                if magic == b"MSLHSTA1" and ver == 1:
                    continue
            except OSError:
                pass

        # Rebuild from local `_iso/` extracts (fast; no full ISO rebuild).
        required = [
            ROOT / "_iso" / "PlCo.dat",
            ROOT / "_iso" / ("PlFx.dat" if ch == "fox" else "PlFc.dat"),
        ]
        missing_iso = [p for p in required if not p.exists()]
        if missing_iso:
            raise RuntimeError(
                f"missing required hit status bin for tests: {out} (and cannot rebuild due to missing _iso/ files: {missing_iso}). "
                f"Run: `uv run python -m tools.extraction.build_data --iso-dir _iso --stage grnla --chars fox,falco`"
            )

        # `extract_fighter_hit_status` depends on existing ISO-derived special_msids.
        msids = ROOT / "data" / "special_msids" / f"{ch}.json"
        if not msids.exists():
            subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "tools.extraction.extract_special_msids",
                    "--iso_dir",
                    str(ROOT / "_iso"),
                    "--out_dir",
                    str(ROOT / "data" / "special_msids"),
                    "--chars",
                    ch,
                ],
                check=True,
            )
        if not msids.exists():
            raise RuntimeError(f"failed to generate required special_msids json for hit status rebuild: {msids}")

        subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.extraction.extract_fighter_hit_status",
                "--iso_dir",
                str(ROOT / "_iso"),
                "--melee_decomp",
                str(ROOT / "refs" / "melee"),
                "--special_msids_dir",
                str(ROOT / "data" / "special_msids"),
                "--out_dir",
                str(ROOT / "data" / "hit_status"),
                "--chars",
                ch,
            ],
            check=True,
        )

        if not out.exists():
            raise RuntimeError(f"failed to generate required hit status bin for tests: {out}")

def _ensure_shield_tilt_bins() -> None:
    for ch, prefix in (("fox", "PlFx"), ("falco", "PlFc")):
        out = ROOT / "data" / "shields" / f"{ch}.bin"
        if out.exists():
            try:
                with out.open("rb") as f:
                    magic = f.read(8)
                    ver = int.from_bytes(f.read(4), "little", signed=False)
                if magic == b"MSLSHLD1" and ver == 4:
                    continue
            except OSError:
                pass

        # Rebuild from local `_iso/` extracts (fast; no full ISO rebuild).
        # Transitive ISO deps for tools.extraction.extract_shield_tilt_table:
        # - It imports extract_fighter_anims and uses:
        #   - _load_parts_table -> reads _iso/PlCo.dat
        #   - _read_rest_srt_and_parents -> reads costume skeleton _iso/Pl*Nr.dat
        #   - _msid_anim_entry / _read_model_scale_and_inv_part -> reads _iso/Pl*.dat
        #   - figatree payload -> reads _iso/Pl*AJ.dat
        required = [
            ROOT / "_iso" / "PlCo.dat",
            ROOT / "_iso" / f"{prefix}.dat",
            ROOT / "_iso" / f"{prefix}Nr.dat",
            ROOT / "_iso" / f"{prefix}AJ.dat",
        ]
        missing_iso = [p for p in required if not p.exists()]
        if missing_iso:
            raise RuntimeError(
                f"missing required shield tilt bin for tests: {out} (and cannot rebuild due to missing _iso/ files: {missing_iso}). "
                f"Run: `uv run python -m tools.extraction.build_data --iso-dir _iso --stage grnla --chars fox,falco`"
            )

        subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.extraction.extract_shield_tilt_table",
                "--iso-dir",
                str(ROOT / "_iso"),
                "--character",
                ch,
                "--out",
                str(out),
            ],
            check=True,
        )

        if not out.exists():
            raise RuntimeError(f"failed to generate required shield tilt bin for tests: {out}")


def _ensure_motion_state_owner_bins() -> None:
    stale = False
    for ch in ("fox", "falco"):
        out = ROOT / "data" / "motion_state" / "owners" / f"{ch}.bin"
        if out.exists():
            try:
                with out.open("rb") as f:
                    magic = f.read(8)
                    ver = int.from_bytes(f.read(4), "little", signed=False)
                if magic == b"MSLMSO01" and ver == 2:
                    continue
            except OSError:
                pass
        stale = True
        break
    manifest = ROOT / "data" / "motion_state" / "owners" / "callback_symbols.json"
    if stale or not manifest.exists():
        subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.extraction.extract_motion_state_owners",
                "--melee_decomp",
                str(ROOT / "refs" / "melee"),
                "--out_dir",
                str(ROOT / "data" / "motion_state" / "owners"),
                "--chars",
                "fox,falco",
            ],
            check=True,
        )
    for ch in ("fox", "falco"):
        out = ROOT / "data" / "motion_state" / "owners" / f"{ch}.bin"
        if not out.exists():
            raise RuntimeError(f"failed to generate required MotionState owner table: {out}")


def _ensure_known_data_artifacts() -> None:
    expected = [
        (ROOT / "data" / "stages" / "bin" / "grnla.bin", b"MSLSTG01", 1),
        (ROOT / "data" / "model_parts" / "fox.bin", b"MSLPART1", 1),
        (ROOT / "data" / "model_parts" / "falco.bin", b"MSLPART1", 1),
        (ROOT / "data" / "items" / "articles" / "fox_falco.bin", b"MSLITAR1", 2),
        (ROOT / "data" / "scripts" / "fox.bin", b"MSLFTSC1", 1),
        (ROOT / "data" / "scripts" / "falco.bin", b"MSLFTSC1", 1),
    ]
    stale = False
    for path, magic, version in expected:
        if not path.exists():
            stale = True
            break
        try:
            with path.open("rb") as f:
                got_magic = f.read(8)
                got_version = int.from_bytes(f.read(4), "little", signed=False)
            if got_magic != magic or got_version != version:
                stale = True
                break
        except OSError:
            stale = True
            break
    if not stale:
        return

    missing_iso = [p for p in (ROOT / "_iso" / "GrNLa.dat", ROOT / "_iso" / "PlCo.dat", ROOT / "_iso" / "PlFx.dat", ROOT / "_iso" / "PlFc.dat") if not p.exists()]
    if missing_iso:
        raise RuntimeError(
            "missing required known-data artifact(s), and cannot rebuild because _iso inputs are missing: "
            f"{missing_iso}. Run: `uv run python -m tools.extraction.build_data --iso-dir _iso --stage grnla --chars fox,falco`"
        )
    subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.extraction.build_data",
            "--iso-dir",
            str(ROOT / "_iso"),
            "--stage",
            "grnla",
            "--chars",
            "fox,falco",
        ],
        check=True,
    )
    for path, _magic, _version in expected:
        if not path.exists():
            raise RuntimeError(f"failed to generate required known-data artifact: {path}")


def pytest_sessionstart(session) -> None:  # type: ignore[no-untyped-def]
    _ensure_motion_state_owner_bins()
    _ensure_dyn_bins()
    _ensure_tracks_bins()
    _ensure_ecb_bottom_tables()
    _ensure_ecb_extents_tables()
    _ensure_hurtcaps_bins()
    _ensure_hurtbox_states_bins()
    _ensure_hit_status_bins()
    _ensure_shield_tilt_bins()
    _ensure_known_data_artifacts()
