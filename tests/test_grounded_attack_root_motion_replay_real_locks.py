from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


def _track_root_flags(path: Path) -> dict[int, int]:
    with path.open("rb") as f:
        if f.read(8) != b"SSANIMT1":
            raise ValueError(f"{path}: bad SSANIMT1 magic")
        (version,) = struct.unpack("<I", f.read(4))
        if version != 3:
            raise ValueError(f"{path}: unsupported SSANIMT1 version={version} (want 3)")
        local_count, anim_count = struct.unpack("<HH", f.read(4))
        f.read(local_count)  # local_parts
        f.read(2 * local_count)  # local_parent
        f.read(4 * local_count)  # local_flags

        roots: dict[int, int] = {}
        for _ in range(anim_count):
            (msid,) = struct.unpack("<H", f.read(2))
            f.read(4)  # end_frame
            f.read(1)  # aobj_loop
            (uses_root_motion,) = struct.unpack("<B", f.read(1))
            roots[int(msid)] = int(uses_root_motion)
            for _li in range(local_count):
                part_record = f.read(2)
                if len(part_record) != 2:
                    raise ValueError(f"{path}: truncated local track record")
                _part, n_tracks = struct.unpack("<BB", part_record)
                for _ti in range(n_tracks):
                    hdr = f.read(8)
                    if len(hdr) != 8:
                        raise ValueError(f"{path}: truncated track header")
                    *_unused, length = struct.unpack("<BBBBHH", hdr)
                    f.read(int(length))
        return roots


def _run_rollout_window(dataset_path: Path, *, start_record: int, window_records: tuple[int, ...]):
    binding = pytest.importorskip("msl_binding")
    ds = read_dataset(str(dataset_path))
    samples = ds.samples
    assert int(samples.shape[0]) > max(window_records)

    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    assert compare_stride == COMPARE_DTYPE.itemsize

    seed_bytes = samples[start_record : start_record + 1]["seed_t"].view("u1").reshape(1, seed_stride).copy()
    out_bytes = np.empty((1, compare_stride), dtype=np.uint8)

    handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
    outs = {}
    try:
        binding.reseed_seed_rollout(handle, seed_bytes)
        for rec in range(start_record, max(window_records) + 1):
            row = samples[rec : rec + 1]
            prev_input = row["prev_input_t"].view("u1").reshape(1, input_stride).copy()
            cur_input = row["input_t"].view("u1").reshape(1, input_stride).copy()
            binding.step_input(handle, prev_input, cur_input)
            binding.write_compare(handle, out_bytes)
            if rec in window_records:
                outs[rec] = out_bytes.view(COMPARE_DTYPE).reshape((1,))[0].copy()
    finally:
        binding.destroy(handle)

    return ds, outs


@pytest.mark.integration
def test_ssanimt1_root_motion_flags_distinguish_attack11_from_attackdash() -> None:
    # SSANIMT1 v3 carries `ftData_80085FD4_ret.x10_b0` so ft_80085030/ft_800850E0 can follow
    # decomp's root-motion branch instead of guessing from TransN track presence.
    #
    # refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    # refs/melee/src/melee/ft/ft_081B.c::{ft_80085030,ft_800850E0}
    root = Path(__file__).resolve().parents[1]
    for ch in ("fox", "falco"):
        tracks_path = root / "data" / "anims" / f"{ch}.tracks.bin"
        if not tracks_path.exists():
            pytest.skip(f"missing local tracks artifact: {tracks_path}")
        flags = _track_root_flags(tracks_path)
        assert flags[46] == 0  # ftCo_SM_Attack11: friction fallback
        assert flags[52] == 1  # ftCo_SM_AttackDash: root-motion branch


@pytest.mark.integration
def test_prh_attack11_from_squat_keeps_friction_and_does_not_chain_on_entry_a_edge() -> None:
    # PRH rollout window from the aggregate F01 disruptive cluster:
    # - Squat enters Attack11 on an A edge at rec5014.
    # - Attack11 has a TransN track, but SSANIMT1 `uses_root_motion=0`, so ft_80085030 takes the
    #   ground-friction fallback and preserves the Squat carry speed.
    # - The same A edge must not seed mv.co.attack1.x0 for an immediate Attack11_IASA chain; the
    #   replay remains in Attack11 through the set_jab_combo command window.
    #
    # refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack1.c::{checkAttack11,ftCo_Attack11_IASA}
    # refs/melee/src/melee/ft/ft_081B.c::ft_80085030
    root = Path(__file__).resolve().parents[1]
    dataset_rel = "datasets/aggregate_recent/replays/validation/aggregate_recent/PositiveRevolvingHyena.msl"
    dataset_path = root / dataset_rel
    if not dataset_path.exists():
        pytest.skip(f"missing local dataset: {dataset_rel}")

    ds, outs = _run_rollout_window(
        dataset_path,
        start_record=4990,
        window_records=tuple(range(5014, 5024)),
    )

    p = 0
    for rec in range(5014, 5024):
        ref = ds.samples[rec : rec + 1]["ref_t1"][0]
        out = outs[rec]
        assert int(out["action_id"][p]) == int(ref["action_id"][p]), rec
        assert int(out["action_frame"][p]) == int(ref["action_frame"][p]), rec
        assert int(out["animation_index"][p]) == int(ref["animation_index"][p]), rec
        assert int(out["state_flags"][p, 0]) == int(ref["state_flags"][p, 0]), rec
        assert float(out["speed_ground_x_self"][p]) == pytest.approx(
            float(ref["speed_ground_x_self"][p]), abs=1e-6
        ), rec
        assert float(out["pos_x"][p]) == pytest.approx(float(ref["pos_x"][p]), abs=1e-6), rec

    assert int(outs[5019]["action_id"][p]) == 44  # ftCo_MS_Attack11, not early Attack12
    assert int(outs[5019]["state_flags"][p, 0]) & 0x40  # set_jab_combo active, x0 remains false
