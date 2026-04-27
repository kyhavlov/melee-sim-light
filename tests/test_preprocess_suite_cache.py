from __future__ import annotations

import json
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
