from __future__ import annotations

from pathlib import Path

from tools.slippi.slpz import (
    EVENT_PAYLOADS,
    GAME_START,
    RAW_HEADER,
    compress_path,
    compress_slpz,
    decompress_path,
    decompress_slpz,
    replay_path_for_peppi,
    resolve_replay_path,
)


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
