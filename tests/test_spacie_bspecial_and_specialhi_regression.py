from __future__ import annotations

from dataclasses import dataclass
import json
from pathlib import Path

import numpy as np
import pytest

from tools.eval.dataset import COMPARE_DTYPE, read_dataset


@dataclass(frozen=True)
class _Case:
    rel: str
    record: int
    player: int
    cluster: str
    check_resolver_entry: bool
    resolver_kind: str  # "up" | "side" | ""
    check_action_frame: bool


def _clamp_stick_i8(v: int) -> int:
    if v < -80:
        return -80
    if v > 80:
        return 80
    return v


def _stick_to_unit_from_input(v: int, deadzone: float) -> float:
    x = float(_clamp_stick_i8(int(v))) / 80.0
    if abs(x) < deadzone:
        return 0.0
    return x


@pytest.mark.integration
def test_spacie_bspecial_entry_and_specialhi_progression_regression() -> None:
    root = Path(__file__).resolve().parents[1]
    base = "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"

    required = [
        "data/anims_ecb/fox.bin",
        "data/anims_ecb/falco.bin",
        "data/characters/fox.json",
        "data/characters/falco.json",
        "data/ecb/fox_bottom.bin",
        "data/ecb/fox_extents.bin",
        "data/ecb/falco_bottom.bin",
        "data/ecb/falco_extents.bin",
    ]
    missing = [path for path in required if not (root / path).exists()]
    if missing:
        pytest.skip(f"missing local data artifacts: {', '.join(missing)}")

    required_firefox_hold_attr_keys = (
        "firefox_hold_gravity_delay_frames",
        "firefox_hold_air_friction",
        "firefox_hold_air_fall_accel",
        "firefox_direction_stick_range_min",
        "firefox_launch_speed",
        "firefox_facing_stick_range_min",
        "firefox_bound_angle_degrees",
    )
    for rel in ("data/characters/fox.json", "data/characters/falco.json"):
        attrs = json.loads((root / rel).read_text(encoding="utf-8"))
        missing_keys = [k for k in required_firefox_hold_attr_keys if k not in attrs]
        if missing_keys:
            pytest.skip(f"stale character attrs ({rel}) missing keys: {', '.join(missing_keys)}")

    cases = [
        # 354->344: should resolve to Up-B hold-air on B-edge + up input.
        _Case(f"{base}/AttachedGoodNaturedGuanaco.msl", 2743, 1, "354->344", True, "up", False),
        _Case(f"{base}/TreasuredBackKangaroo.msl", 3192, 1, "354->344", True, "up", False),
        # 350->344: should resolve to Side-B air start on B-edge + side input.
        _Case(f"{base}/AttachedGoodNaturedGuanaco.msl", 2312, 0, "350->344", True, "side", False),
        _Case(f"{base}/TreasuredBackKangaroo.msl", 672, 1, "350->344", True, "side", False),
        # 356->354: hold-air should progress into air-hi.
        _Case(f"{base}/AttachedGoodNaturedGuanaco.msl", 2602, 1, "356->354", False, "", True),
        _Case(f"{base}/TreasuredBackKangaroo.msl", 3336, 1, "356->354", False, "", True),
        # 252->354: hold-air should progress to grounded hi action in these suite records.
        _Case(f"{base}/AttachedGoodNaturedGuanaco.msl", 2758, 1, "252->354", False, "", False),
        _Case(f"{base}/TreasuredBackKangaroo.msl", 3207, 1, "252->354", False, "", False),
    ]

    common = json.loads((root / "data/common/ft_common_data.json").read_text(encoding="utf-8"))
    x_thresh = float(common["special_stick_x_threshold_side"])
    y_thresh = float(common["special_stick_y_threshold"])
    deadzone_x = float(common["lstick_deadzone_x"])
    deadzone_y = float(common["lstick_deadzone_y"])

    by_rel: dict[str, list[_Case]] = {}
    for case in cases:
        by_rel.setdefault(case.rel, []).append(case)

    binding = pytest.importorskip("msl_binding")
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    for rel, rel_cases in by_rel.items():
        dataset_path = root / rel
        if not dataset_path.exists():
            pytest.skip(f"missing local dataset: {rel}")
        ds = read_dataset(str(dataset_path))
        samples = ds.samples
        num_records = int(samples.shape[0])

        for case in rel_cases:
            assert num_records > case.record, f"dataset too short for record={case.record}: {rel}"
            row = samples[case.record]
            p = case.player

            seed_action = int(row["seed_t"]["action_id"][p])
            ref_action = int(row["ref_t1"]["action_id"][p])
            ref_anim = int(row["ref_t1"]["animation_index"][p])
            ref_action_frame = int(row["ref_t1"]["action_frame"][p])

            if case.check_resolver_entry:
                prev_buttons = int(row["prev_input_t"]["p"][p]["buttons"])
                cur_buttons = int(row["input_t"]["p"][p]["buttons"])
                b_edge = ((cur_buttons & 0x0200) != 0) and ((prev_buttons & 0x0200) == 0)
                assert b_edge, f"{rel} record={case.record} p={p} expected B-edge for {case.cluster}"

                stick_x = _stick_to_unit_from_input(int(row["input_t"]["p"][p]["main_x"]), deadzone_x)
                stick_y = _stick_to_unit_from_input(int(row["input_t"]["p"][p]["main_y"]), deadzone_y)
                if case.resolver_kind == "up":
                    assert (
                        stick_y >= y_thresh
                    ), f"{rel} record={case.record} p={p} expected up-threshold stick_y>=x21C"
                elif case.resolver_kind == "side":
                    assert (
                        abs(stick_x) >= x_thresh
                    ), f"{rel} record={case.record} p={p} expected side-threshold |stick_x|>=x218"
                    assert (
                        stick_y < y_thresh
                    ), f"{rel} record={case.record} p={p} expected side resolver (not up threshold)"
                assert seed_action in {27, 28}, f"{rel} record={case.record} p={p} unexpected seed action={seed_action}"
            else:
                assert seed_action == 354, f"{rel} record={case.record} p={p} expected seed action_id=354"

            handle = binding.init(batch_size=1, num_players=int(ds.header["num_players"]))
            try:
                seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
                prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
                input_bytes = np.empty((1, input_stride), dtype=np.uint8)
                out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

                seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                    1, seed_stride
                )
                prev_input_bytes[:] = np.frombuffer(
                    row["prev_input_t"].tobytes(order="C"), dtype=np.uint8
                ).reshape(1, input_stride)
                input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
                    1, input_stride
                )

                binding.reseed_seed(handle, seed_bytes)
                binding.step_input(handle, prev_input_bytes, input_bytes)
                binding.write_compare(handle, out_compare_bytes)

                out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)
                out_action = int(out["action_id"][0, p])
                out_anim = int(out["animation_index"][0, p])
                out_action_frame = int(out["action_frame"][0, p])

                assert (
                    out_action == ref_action
                ), f"{rel} record={case.record} p={p} {case.cluster} expected action_id={ref_action}, got {out_action}"
                assert (
                    out_anim == ref_anim
                ), f"{rel} record={case.record} p={p} {case.cluster} expected animation_index={ref_anim}, got {out_anim}"
                if case.check_action_frame:
                    assert (
                        out_action_frame == ref_action_frame
                    ), f"{rel} record={case.record} p={p} {case.cluster} expected action_frame={ref_action_frame}, got {out_action_frame}"

                if case.check_resolver_entry and ref_action in (350, 354):
                    # Strict replay-real lock: aerial Side-B/Up-B resolver entries clear vertical
                    # self velocity on enter.
                    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSStart_Enter
                    # refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHiStart_Enter
                    assert abs(float(out["speed_y_self"][0, p]) - float(row["ref_t1"]["speed_y_self"][p])) <= 1e-6
            finally:
                binding.destroy(handle)
