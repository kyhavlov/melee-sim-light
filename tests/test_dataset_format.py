from __future__ import annotations

from pathlib import Path

import numpy as np
import pytest

from tools.eval import dataset as ds


def test_dataset_roundtrip(tmp_path: Path) -> None:
    samples = np.zeros(3, dtype=ds.SAMPLE_DTYPE)
    out = tmp_path / "t.msl"
    ds.write_dataset(str(out), num_players=2, samples=samples)

    got = ds.read_dataset(str(out))
    assert int(got.header["num_players"]) == 2
    assert int(got.header["num_records"]) == 3
    assert int(got.header["record_size"]) == ds.SAMPLE_DTYPE.itemsize
    assert got.samples.dtype == ds.SAMPLE_DTYPE
    assert got.samples.shape == (3,)


def test_dataset_window_uses_memmap_slice(tmp_path: Path) -> None:
    samples = np.zeros(5, dtype=ds.SAMPLE_DTYPE)
    samples["seed_t"]["frame_id"] = np.arange(5, dtype=np.int32)
    out = tmp_path / "t.msl"
    ds.write_dataset(str(out), num_players=2, samples=samples)

    got = ds.read_dataset_window(str(out), 2, 4)

    assert int(got.header["num_records"]) == 5
    assert isinstance(got.samples, np.memmap)
    assert got.samples.shape == (2,)
    assert got.samples["seed_t"]["frame_id"].tolist() == [2, 3]


def test_dataset_rejects_bad_magic(tmp_path: Path) -> None:
    out = tmp_path / "bad.msl"
    out.write_bytes(b"NOTMAGIC" + b"\x00" * 64)
    with pytest.raises(ValueError, match="bad magic"):
        ds.read_dataset(str(out))


def test_dataset_rejects_record_size_mismatch(tmp_path: Path) -> None:
    samples = np.zeros(1, dtype=ds.SAMPLE_DTYPE)
    out = tmp_path / "bad_record_size.msl"

    header = np.zeros((), dtype=ds.HEADER_DTYPE)
    header["magic"] = ds.MAGIC
    header["record_size"] = ds.SAMPLE_DTYPE.itemsize + 4
    header["num_records"] = 1
    header["num_players"] = 2

    out.write_bytes(header.tobytes(order="C") + samples.tobytes(order="C"))
    with pytest.raises(ValueError, match="record_size mismatch"):
        ds.read_dataset(str(out))


def test_dataset_rejects_truncated_samples(tmp_path: Path) -> None:
    out = tmp_path / "trunc.msl"
    header = np.zeros((), dtype=ds.HEADER_DTYPE)
    header["magic"] = ds.MAGIC
    header["record_size"] = ds.SAMPLE_DTYPE.itemsize
    header["num_records"] = 2
    header["num_players"] = 2
    out.write_bytes(header.tobytes(order="C") + b"\x00" * (ds.SAMPLE_DTYPE.itemsize + 7))
    with pytest.raises(ValueError, match="file truncated"):
        ds.read_dataset(str(out))
