from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
NORMAL_VALIDATION_TARGETS = (
    "validate",
    "validate-aggregate",
    "validate-rollout",
    "validate-rollout-aggregate",
    "validate-marth",
    "validate-rollout-marth",
    "validate-sheik",
    "validate-rollout-sheik",
    "validate-all",
    "validate-heldout",
)
NORMAL_VALIDATION_MODULES = (
    "tools/eval/run_one_step_suite_eval.py",
    "tools/eval/run_rollout_suite_eval.py",
    "tools/eval/run_validate_all.py",
    "tools/eval/run_heldout_validation.py",
)


def _make_target_block(makefile: str, target: str) -> str:
    marker = f"\n{target}:"
    start = makefile.find(marker)
    assert start >= 0, f"missing Makefile target {target}"
    end = len(makefile)
    for idx in range(start + 1, len(makefile)):
        if makefile[idx] == "\n" and idx + 1 < len(makefile) and makefile[idx + 1] not in ("\t", "\n", "#"):
            end = idx
            break
    return makefile[start:end]


def test_normal_validation_make_targets_do_not_pass_dataset_cache_flags() -> None:
    makefile = (ROOT / "Makefile").read_text(encoding="utf-8")
    for target in NORMAL_VALIDATION_TARGETS:
        block = _make_target_block(makefile, target)
        assert "--datasets-dir" not in block
        assert "cached-datasets" not in block
        assert "in-memory-preprocess" not in block
        assert "preprocess_suite" not in block


def test_normal_validation_modules_do_not_expose_msl_cache_options() -> None:
    banned = ("--datasets-dir", "--cached-datasets", "--in-memory-preprocess", "preprocess_suite")
    for rel in NORMAL_VALIDATION_MODULES:
        text = (ROOT / rel).read_text(encoding="utf-8")
        for needle in banned:
            assert needle not in text, f"{rel} still exposes {needle}"


def test_preprocess_suite_cache_writer_is_deleted_from_normal_tooling() -> None:
    from tools.slippi import make_dataset_from_slp

    assert not (ROOT / "tools/slippi/preprocess_suite.py").exists()
    assert not hasattr(make_dataset_from_slp, "write_dataset_from_slp")
