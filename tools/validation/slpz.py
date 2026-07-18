from __future__ import annotations

import argparse
import struct
import tempfile
from contextlib import contextmanager
from pathlib import Path
from typing import Callable, Iterator


RAW_HEADER = b"{U\x03raw[$U#l"
EVENT_PAYLOADS = 0x35
GAME_START = 0x36
VERSION = 0
_native_unorder_events: Callable[[bytes, list[int]], bytes] | None = None


class SlpzError(ValueError):
    pass


def set_native_unorder_events(callback: Callable[[bytes, list[int]], bytes] | None) -> None:
    global _native_unorder_events
    _native_unorder_events = callback


def _require_zstandard():
    try:
        import zstandard as zstd
    except ImportError as exc:
        raise RuntimeError(
            "Reading or writing .slpz replays requires the `zstandard` Python package. "
            "Install the dev environment with `uv sync --dev`."
        ) from exc
    return zstd


def resolve_replay_path(path: str | Path) -> Path:
    """Resolve a replay path, accepting .slp paths whose stored file is .slpz."""
    p = Path(path)
    if p.exists():
        return p
    if p.suffix == ".slp":
        compressed = p.with_suffix(".slpz")
        if compressed.exists():
            return compressed
    if p.suffix == ".slpz":
        uncompressed = p.with_suffix(".slp")
        if uncompressed.exists():
            return uncompressed
    return p


@contextmanager
def replay_path_for_peppi(path: str | Path) -> Iterator[Path]:
    """Yield a .slp path suitable for peppi, decompressing .slpz to a temp file."""
    replay = resolve_replay_path(path)
    if replay.suffix != ".slpz":
        yield replay
        return

    with tempfile.TemporaryDirectory(prefix="msl_slpz_") as td:
        out = Path(td) / (replay.stem + ".slp")
        out.write_bytes(decompress_slpz(replay.read_bytes()))
        yield out


def _event_sizes(event_payloads: bytes) -> tuple[list[int], int]:
    if len(event_payloads) < 2 or event_payloads[0] != EVENT_PAYLOADS:
        raise SlpzError("invalid Slippi event payloads section")
    info_size = event_payloads[1]
    if info_size < 1 or (info_size - 1) % 3 != 0:
        raise SlpzError("invalid Slippi event payloads size")
    event_count = (info_size - 1) // 3
    total_size = 2 + event_count * 3
    if len(event_payloads) < total_size:
        raise SlpzError("truncated Slippi event payloads section")

    sizes = [0] * 256
    for i in range(event_count):
        offset = 2 + i * 3
        command = event_payloads[offset]
        sizes[command] = struct.unpack(">H", event_payloads[offset + 1 : offset + 3])[0]
    return sizes, event_count


def _event_counts(events: bytes, sizes: list[int]) -> list[int]:
    counts = [0] * 256
    i = 0
    while i < len(events):
        command = events[i]
        size = sizes[command]
        if size <= 0 or i + 1 + size > len(events):
            raise SlpzError("invalid Slippi event stream")
        counts[command] += 1
        i += 1 + size
    return counts


def _reordered_offsets(sizes: list[int], counts: list[int]) -> list[int]:
    offsets = [0] * 256
    running = 0
    for command in range(256):
        offsets[command] = running
        running += sizes[command] * counts[command]
    return offsets


def _reorder_events(events: bytes, sizes: list[int]) -> bytes:
    counts = _event_counts(events, sizes)
    total_events = sum(counts)
    offsets = _reordered_offsets(sizes, counts)
    payload_size = sum(sizes[command] * counts[command] for command in range(256))
    event_order_offset = 4
    reordered_offset = event_order_offset + total_events
    out = bytearray(reordered_offset + payload_size)
    out[0:4] = struct.pack(">I", total_events)

    written = [0] * 256
    event_index = 0
    i = 0
    while i < len(events):
        command = events[i]
        size = sizes[command]
        stride = counts[command]
        out[event_order_offset + event_index] = command
        write_start = reordered_offset + offsets[command] + written[command]
        payload = events[i + 1 : i + 1 + size]
        for j, value in enumerate(payload):
            out[write_start + j * stride] = value
        written[command] += 1
        event_index += 1
        i += 1 + size

    return bytes(out)


def _unorder_events_python(data: bytes, sizes: list[int]) -> bytes:
    if len(data) < 4:
        raise SlpzError("truncated reordered event stream")
    total_events = struct.unpack(">I", data[:4])[0]
    event_order_offset = 4
    reordered_offset = event_order_offset + total_events
    if len(data) < reordered_offset:
        raise SlpzError("truncated reordered event order")

    event_order = data[event_order_offset:reordered_offset]
    counts = [0] * 256
    for command in event_order:
        counts[command] += 1
    offsets = _reordered_offsets(sizes, counts)
    payloads = data[reordered_offset:]
    expected_payload_size = sum(sizes[command] * counts[command] for command in range(256))
    if len(payloads) != expected_payload_size:
        raise SlpzError("invalid reordered event payload size")

    out_size = total_events + expected_payload_size
    out = bytearray(out_size)
    written = [0] * 256
    out_i = 0
    for command in event_order:
        size = sizes[command]
        stride = counts[command]
        out[out_i] = command
        read_start = offsets[command] + written[command]
        for j in range(size):
            out[out_i + 1 + j] = payloads[read_start + j * stride]
        written[command] += 1
        out_i += 1 + size
    return bytes(out)


def _unorder_events(data: bytes, sizes: list[int]) -> bytes:
    if _native_unorder_events is not None:
        try:
            return _native_unorder_events(data, sizes)
        except ValueError as exc:
            raise SlpzError(str(exc)) from exc
    return _unorder_events_python(data, sizes)


def compress_slpz(slp: bytes, *, level: int = 3) -> bytes:
    if len(slp) < 16 or slp[: len(RAW_HEADER)] != RAW_HEADER:
        raise SlpzError("input does not look like a Slippi .slp raw stream")

    raw_len = struct.unpack(">I", slp[11:15])[0]
    metadata_offset = 15 + raw_len
    if metadata_offset > len(slp):
        raise SlpzError("invalid Slippi raw length")

    sizes, event_type_count = _event_sizes(slp[15:])
    event_sizes_size = 2 + event_type_count * 3
    event_sizes_payload = slp[15 : 15 + event_sizes_size]

    game_start_offset = 15 + event_sizes_size
    game_start_size = sizes[GAME_START] + 1
    if game_start_offset + game_start_size > metadata_offset or slp[game_start_offset] != GAME_START:
        raise SlpzError("invalid Slippi game start section")

    other_events_offset = game_start_offset + game_start_size
    reordered_events = _reorder_events(slp[other_events_offset:metadata_offset], sizes)
    compressed_events = _require_zstandard().ZstdCompressor(level=level).compress(reordered_events)

    game_start_payload = slp[game_start_offset:other_events_offset]
    metadata = slp[metadata_offset:]

    out = bytearray(24)
    out[0:4] = struct.pack(">I", VERSION)
    out[4:8] = struct.pack(">I", len(out))
    out.extend(event_sizes_payload)
    out[8:12] = struct.pack(">I", len(out))
    out.extend(game_start_payload)
    out[12:16] = struct.pack(">I", len(out))
    out.extend(metadata)
    out[16:20] = struct.pack(">I", len(out))
    out[20:24] = struct.pack(">I", len(reordered_events))
    out.extend(compressed_events)
    return bytes(out)


def decompress_slpz(slpz: bytes) -> bytes:
    if len(slpz) < 24:
        raise SlpzError("input is too small to be .slpz")

    version, event_sizes_offset, game_start_offset, metadata_offset, compressed_offset, event_data_size = (
        struct.unpack(">IIIIII", slpz[:24])
    )
    if version > VERSION:
        raise SlpzError(f"unsupported .slpz version: {version}")
    if not (24 <= event_sizes_offset <= game_start_offset <= metadata_offset <= compressed_offset <= len(slpz)):
        raise SlpzError("invalid .slpz section offsets")

    event_sizes_payload = slpz[event_sizes_offset:game_start_offset]
    sizes, _event_count = _event_sizes(event_sizes_payload)
    event_data = _require_zstandard().ZstdDecompressor().decompress(
        slpz[compressed_offset:], max_output_size=event_data_size
    )
    if len(event_data) != event_data_size:
        raise SlpzError("invalid decompressed .slpz event size")

    out = bytearray()
    out.extend(RAW_HEADER)
    out.extend(b"\0\0\0\0")
    out.extend(event_sizes_payload)
    out.extend(slpz[game_start_offset:metadata_offset])
    out.extend(_unorder_events(event_data, sizes))
    raw_len = len(out) - 15
    out.extend(slpz[metadata_offset:compressed_offset])
    out[11:15] = struct.pack(">I", raw_len)
    return bytes(out)


def compress_path(src: Path, dst: Path | None = None, *, force: bool = False, level: int = 3) -> Path:
    src = Path(src)
    dst = src.with_suffix(".slpz") if dst is None else Path(dst)
    if dst.exists() and not force:
        raise FileExistsError(dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(compress_slpz(src.read_bytes(), level=level))
    return dst


def decompress_path(src: Path, dst: Path | None = None, *, force: bool = False) -> Path:
    src = Path(src)
    dst = src.with_suffix(".slp") if dst is None else Path(dst)
    if dst.exists() and not force:
        raise FileExistsError(dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(decompress_slpz(src.read_bytes()))
    return dst


def main() -> None:
    ap = argparse.ArgumentParser(description="Compress or decompress Slippi .slp/.slpz replay files.")
    ap.add_argument("mode", choices=("compress", "decompress"))
    ap.add_argument("paths", nargs="+", type=Path)
    ap.add_argument("--out", type=Path, default=None, help="Output file path, valid with one input path.")
    ap.add_argument("--force", action="store_true", help="Overwrite existing output files.")
    ap.add_argument("--delete-source", action="store_true", help="Delete each input after successful conversion.")
    ap.add_argument("--level", type=int, default=3, help="zstd compression level for compression.")
    args = ap.parse_args()

    if args.out is not None and len(args.paths) != 1:
        raise SystemExit("--out can only be used with one input path")

    for src in args.paths:
        if args.mode == "compress":
            out = compress_path(src, args.out, force=bool(args.force), level=int(args.level))
        else:
            out = decompress_path(src, args.out, force=bool(args.force))
        if bool(args.delete_source):
            src.unlink()
        print(f"{src} -> {out}")


if __name__ == "__main__":
    main()
