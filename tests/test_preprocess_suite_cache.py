from __future__ import annotations

import json
import os
from pathlib import Path

import numpy as np

from tools.eval.dataset import SAMPLE_DTYPE, write_dataset
from tools.slippi import preprocess_suite


def test_dataset_record_size_reads_current_dataset_header(tmp_path: Path) -> None:
    path = tmp_path / "sample.msl"
    write_dataset(str(path), num_players=2, samples=np.zeros(1, dtype=SAMPLE_DTYPE))

    assert preprocess_suite._dataset_record_size(path) == SAMPLE_DTYPE.itemsize


def test_dataset_cache_signature_match_roundtrip(tmp_path: Path) -> None:
    meta_path = tmp_path / "sample.msl.meta.json"
    signature = {"cache_version": 2, "sample_dtype_itemsize": SAMPLE_DTYPE.itemsize}

    preprocess_suite._write_dataset_cache_meta(meta_path, signature, rebuilt=True)

    assert preprocess_suite._signature_matches(meta_path, signature)
    assert not preprocess_suite._signature_matches(meta_path, {**signature, "sample_dtype_itemsize": -1})
    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    assert meta["rebuilt"] is True
    assert meta["signature"] == signature


def test_legacy_dataset_without_meta_rebuilds_by_default_even_if_mtime_and_size_match(tmp_path: Path) -> None:
    slp_path = tmp_path / "replay.slp"
    dataset_path = tmp_path / "sample.msl"
    meta_path = tmp_path / "sample.msl.meta.json"
    signature = {"cache_version": 2, "sample_dtype_itemsize": SAMPLE_DTYPE.itemsize}
    slp_path.write_bytes(b"slp")
    write_dataset(str(dataset_path), num_players=2, samples=np.zeros(1, dtype=SAMPLE_DTYPE))
    old_time = 1_700_000_000
    new_time = old_time + 60
    slp_path.touch()
    dataset_path.touch()
    os.utime(slp_path, (old_time, old_time))
    os.utime(dataset_path, (new_time, new_time))

    assert (
        preprocess_suite._dataset_cache_action(
            force=False,
            out_path=dataset_path,
            slp_path=slp_path,
            dataset_meta_path=meta_path,
            signature=signature,
            trust_legacy_cache=False,
        )
        == "rebuild"
    )


def test_dataset_with_matching_meta_skips(tmp_path: Path) -> None:
    slp_path = tmp_path / "replay.slp"
    dataset_path = tmp_path / "sample.msl"
    meta_path = tmp_path / "sample.msl.meta.json"
    signature = {"cache_version": 2, "sample_dtype_itemsize": SAMPLE_DTYPE.itemsize}
    slp_path.write_bytes(b"slp")
    write_dataset(str(dataset_path), num_players=2, samples=np.zeros(1, dtype=SAMPLE_DTYPE))
    preprocess_suite._write_dataset_cache_meta(meta_path, signature, rebuilt=True)

    assert (
        preprocess_suite._dataset_cache_action(
            force=False,
            out_path=dataset_path,
            slp_path=slp_path,
            dataset_meta_path=meta_path,
            signature=signature,
            trust_legacy_cache=False,
        )
        == "skip"
    )


def test_trust_legacy_cache_opt_in_stamps_without_rebuild(tmp_path: Path) -> None:
    slp_path = tmp_path / "replay.slp"
    dataset_path = tmp_path / "sample.msl"
    meta_path = tmp_path / "sample.msl.meta.json"
    signature = {"cache_version": 2, "sample_dtype_itemsize": SAMPLE_DTYPE.itemsize}
    slp_path.write_bytes(b"slp")
    write_dataset(str(dataset_path), num_players=2, samples=np.zeros(1, dtype=SAMPLE_DTYPE))
    old_time = 1_700_000_000
    new_time = old_time + 60
    os.utime(slp_path, (old_time, old_time))
    os.utime(dataset_path, (new_time, new_time))

    assert (
        preprocess_suite._dataset_cache_action(
            force=False,
            out_path=dataset_path,
            slp_path=slp_path,
            dataset_meta_path=meta_path,
            signature=signature,
            trust_legacy_cache=True,
        )
        == "trust_legacy"
    )
