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


def test_cache_signature_changes_when_slippi_helper_dependency_changes(tmp_path: Path) -> None:
    root = tmp_path
    suite_path = root / "replays" / "suites" / "suite.json"
    slp_path = root / "replays" / "example.slp"
    helper_path = root / "tools" / "slippi" / "damage_history.py"
    dataset_py = root / "tools" / "eval" / "dataset.py"
    suite_path.parent.mkdir(parents=True)
    slp_path.parent.mkdir(parents=True, exist_ok=True)
    helper_path.parent.mkdir(parents=True)
    dataset_py.parent.mkdir(parents=True)
    suite_path.write_text("{}", encoding="utf-8")
    slp_path.write_bytes(b"slp")
    dataset_py.write_text("SAMPLE_DTYPE = object()\n", encoding="utf-8")
    helper_path.write_text("VALUE = 1\n", encoding="utf-8")

    kwargs = {
        "root": root,
        "suite_path": suite_path,
        "suite_name": "suite",
        "replay_rel": "replays/example.slp",
        "slp_path": slp_path,
        "ports": [1, 2],
        "ucf_enabled": True,
        "ucf_cardinals_1_0_enabled": True,
    }
    before = preprocess_suite._cache_signature(**kwargs)
    helper_path.write_text("VALUE = 2\n", encoding="utf-8")
    after = preprocess_suite._cache_signature(**kwargs)

    assert before["source_inputs"] != after["source_inputs"]
    changed = [
        (a, b)
        for a, b in zip(before["source_inputs"], after["source_inputs"], strict=True)
        if a != b and a["path"] == "tools/slippi/damage_history.py"
    ]
    assert changed


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


def test_preprocess_suite_auto_workers_uses_parallel_default(monkeypatch) -> None:
    monkeypatch.setattr(preprocess_suite.os, "cpu_count", lambda: 16)

    assert preprocess_suite._resolve_worker_count(0, 3) == 3
    assert preprocess_suite._resolve_worker_count(0, 20) == 16
    assert preprocess_suite._resolve_worker_count(1, 20) == 1
