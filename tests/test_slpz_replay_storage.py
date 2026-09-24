from __future__ import annotations

import struct
from pathlib import Path

import pytest

from tools.validation.slpz import (
    EVENT_PAYLOADS,
    GAME_START,
    RAW_HEADER,
    SlpzError,
    _event_sizes,
    _reorder_events,
    _require_zstandard,
    _unorder_events,
    _unorder_events_python,
    compress_path,
    compress_slpz,
    decompress_path,
    decompress_slpz,
    replay_path_for_peppi,
    resolve_replay_path,
)


@pytest.mark.parametrize("suffix", [".slp", ".slpz"])
def test_missing_lfs_payload_explains_opt_in_download(tmp_path: Path, suffix: str) -> None:
    pointer = b"version https://git-lfs.github.com/spec/v1\noid sha256:" + b"0" * 64 + b"\nsize 123\n"
    replay = tmp_path / f"game{suffix}"
    replay.write_bytes(pointer)
    with pytest.raises(SlpzError, match="git lfs pull"):
        with replay_path_for_peppi(replay):
            pytest.fail("An LFS pointer must not reach the replay parser")
    if suffix == ".slpz":
        with pytest.raises(SlpzError, match="git lfs pull"):
            decompress_slpz(pointer)


def _fake_slp() -> bytes:
    event_payloads = bytes(
        [
            EVENT_PAYLOADS,
            7,
            GAME_START,
            0,
            2,
            0x37,
            0,
            3,
        ]
    )
    game_start = bytes([GAME_START, 0xAA, 0xBB])
    events = bytes([0x37, 1, 2, 3, 0x37, 4, 5, 6])
    raw = event_payloads + game_start + events
    metadata = b"{U\x08metadata"
    return RAW_HEADER + len(raw).to_bytes(4, "big") + raw + metadata


def test_slpz_round_trip_preserves_slp_bytes() -> None:
    slp = _fake_slp()
    assert decompress_slpz(compress_slpz(slp)) == slp


def test_slpz_path_helpers_accept_compressed_replay(tmp_path: Path) -> None:
    slp = tmp_path / "game.slp"
    slpz = tmp_path / "game.slpz"
    roundtrip = tmp_path / "roundtrip.slp"
    slp.write_bytes(_fake_slp())

    compress_path(slp, slpz)
    slp.unlink()

    assert resolve_replay_path(slp) == slpz
    with replay_path_for_peppi(slp) as peppi_path:
        assert peppi_path.suffix == ".slp"
        assert peppi_path.read_bytes() == _fake_slp()

    decompress_path(slpz, roundtrip)
    assert roundtrip.read_bytes() == _fake_slp()


def test_slpz_native_unorder_matches_python_for_multicommand_payload() -> None:
    sizes = [0] * 256
    sizes[0x37] = 3
    sizes[0x38] = 2
    sizes[0x3B] = 5
    events = bytes(
        [
            0x37,
            1,
            2,
            3,
            0x3B,
            4,
            5,
            6,
            7,
            8,
            0x38,
            9,
            10,
            0x37,
            11,
            12,
            13,
            0x38,
            14,
            15,
        ]
    )
    reordered = _reorder_events(events, sizes)

    assert _unorder_events(reordered, sizes) == _unorder_events_python(reordered, sizes) == events


def test_slpz_native_unorder_matches_python_for_real_replay_payload() -> None:
    replay = Path("replays/validation/cardinal_1.0_recent/AttachedGoodNaturedGuanaco.slpz")
    if not replay.exists():
        pytest.skip("representative compressed replay is not present")

    slpz = replay.read_bytes()
    if len(slpz) < 24:
        raise AssertionError("truncated slpz fixture")
    (
        _version,
        event_sizes_offset,
        game_start_offset,
        _metadata_offset,
        event_data_offset,
        event_data_size,
    ) = struct.unpack(">IIIIII", slpz[:24])
    sizes, _event_type_count = _event_sizes(slpz[event_sizes_offset:game_start_offset])
    reordered = _require_zstandard().ZstdDecompressor().decompress(
        slpz[event_data_offset:], max_output_size=event_data_size
    )

    assert _unorder_events(reordered, sizes) == _unorder_events_python(reordered, sizes)
