from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset

_FIXTURE = Path(__file__).resolve().parents[1] / "tests/fixtures/hard_row_lock_pack_seedref.json"


def _skip_if_required_artifacts_missing(root: Path) -> None:
    required = [
        "data/stages/final_destination.json",
        "data/common/ft_common_data.json",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/anims/fox.tracks.bin",
        "data/anims/falco.tracks.bin",
        "data/moves/fox.json",
        "data/moves/falco.json",
    ]
    missing = [rel for rel in required if not (root / rel).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")


def _load_rows() -> list[dict[str, object]]:
    payload = json.loads(_FIXTURE.read_text(encoding="utf-8"))
    rows = payload.get("rows", [])
    if not isinstance(rows, list) or not rows:
        raise RuntimeError(f"lock pack fixture has no rows: {_FIXTURE}")
    return [dict(r) for r in rows]


def _row_id(row: dict[str, object]) -> str:
    ds = Path(str(row["dataset"]))
    return f"{ds.name}:rec{int(row['record'])}:p{int(row['p'])}:{row['field']}"


def _parse_field(field: str) -> tuple[str, int | None]:
    if "[" in field and field.endswith("]"):
        base, rest = field.split("[", 1)
        idx = int(rest[:-1])
        return base, idx
    return field, None


def _scalar_from_seed_or_ref(row_obj: np.ndarray, *, p: int, field: str) -> int:
    base, sub = _parse_field(field)
    if sub is None:
        return int(row_obj[base][p])
    return int(row_obj[base][p, sub])


def _scalar_from_out(out_obj: np.ndarray, *, p: int, field: str) -> int:
    base, sub = _parse_field(field)
    if sub is None:
        return int(out_obj[base][p])
    return int(out_obj[base][p, sub])


@pytest.mark.integration
@pytest.mark.parametrize("lock", _load_rows(), ids=_row_id)
def test_hard_row_lock_pack_seedref_signatures(lock: dict[str, object]) -> None:
    """Fast lock pack over dominant seed==ref residual signatures.

    This intentionally locks current replay-real (seed/ref/out) signatures for high-frequency
    seed==ref rows, so preflight runs can catch guardrail drift before full suite validate.
    """

    root = Path(__file__).resolve().parents[1]
    _skip_if_required_artifacts_missing(root)

    dataset_rel = str(lock["dataset"])
    dataset_path = (root / dataset_rel).resolve()
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    record = int(lock["record"])
    p = int(lock["p"])
    field = str(lock["field"])

    ds = read_dataset(str(dataset_path))
    assert int(ds.header["num_records"]) > record, f"dataset too short: {dataset_rel} rec={record}"

    row = ds.samples[record : record + 1]
    seed = row["seed_t"][0]
    ref = row["ref_t1"][0]

    exp_seed = int(lock["seed"])
    exp_ref = int(lock["ref"])
    exp_out = int(lock["out"])

    got_seed = _scalar_from_seed_or_ref(seed, p=p, field=field)
    got_ref = _scalar_from_seed_or_ref(ref, p=p, field=field)
    assert got_seed == exp_seed, (
        f"fixture drift seed mismatch: {dataset_rel} rec={record} p={p} field={field} "
        f"expected seed={exp_seed} got={got_seed}"
    )
    assert got_ref == exp_ref, (
        f"fixture drift ref mismatch: {dataset_rel} rec={record} p={p} field={field} "
        f"expected ref={exp_ref} got={got_ref}"
    )

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    seed_bytes = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, seed_stride)
    prev_input_bytes = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(
        1, input_stride
    )
    input_bytes = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).copy().reshape(1, input_stride)
    out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    try:
        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
    finally:
        binding.destroy(handle)

    out = out_compare_bytes.view(COMPARE_DTYPE).reshape((1,))[0]
    got_out = _scalar_from_out(out, p=p, field=field)
    assert got_out == exp_out, (
        f"lock drift: {dataset_rel} rec={record} p={p} field={field} "
        f"expected out={exp_out} got={got_out} (seed={got_seed} ref={got_ref})"
    )
