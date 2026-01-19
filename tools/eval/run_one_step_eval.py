from __future__ import annotations

import argparse
import importlib
from dataclasses import dataclass

import numpy as np

from tools.eval.dataset import COMPARE_V0_DTYPE, SEED_V0_DTYPE, INPUT_V0_DTYPE, read_dataset_v0


@dataclass
class FloatMetric:
    mae: float
    p95: float
    mx: float


def _float_metrics(err: np.ndarray) -> FloatMetric:
    err = np.asarray(err, dtype=np.float32).reshape(-1)
    if err.size == 0:
        return FloatMetric(0.0, 0.0, 0.0)
    abs_err = np.abs(err)
    return FloatMetric(
        mae=float(abs_err.mean()),
        p95=float(np.quantile(abs_err, 0.95)),
        mx=float(abs_err.max()),
    )


def _load_binding():
    # Built by `python -m pip install -e python` (or similar).
    return importlib.import_module("msl_binding")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", required=True)
    ap.add_argument("--chunk", type=int, default=4096)
    args = ap.parse_args()

    ds = read_dataset_v0(args.dataset)
    samples = ds.samples
    num_records = samples.shape[0]
    num_players = int(ds.header["num_players"])
    max_items = int(samples.dtype["seed_t"]["items"].shape[0])

    binding = _load_binding()
    sizes = binding.sizes()

    seed_stride = int(sizes["seed_v0"])
    input_stride = int(sizes["input_v0"])
    compare_stride = int(sizes["compare_v0"])

    handle = binding.init(batch_size=min(args.chunk, num_records), num_players=num_players)

    total_records = 0
    total_player_frames = 0
    mismatches = {
        "action_id": 0,
        "action_frame": 0,
        "on_ground": 0,
        "facing": 0,
        "stocks": 0,
        "jumps_left": 0,
        "is_dead": 0,
        "hitlag": 0,
        "hitstun": 0,
        "l_cancel": 0,
        "hurtbox_state": 0,
        "ground_id": 0,
        "animation_index": 0,
        "instance_hit_by": 0,
        "instance_id": 0,
        "last_attack_landed": 0,
        "combo_count": 0,
        "last_hit_by": 0,
        "state_flags": 0,
        "item_exists": 0,
        "item_type": 0,
        "item_state": 0,
        "item_owner": 0,
        "item_instance_id": 0,
    }
    float_err = {
        k: []
        for k in (
            "pos_x",
            "pos_y",
            "speed_air_x_self",
            "speed_ground_x_self",
            "speed_y_self",
            "speed_x_attack",
            "speed_y_attack",
            "percent",
            "shield_hp",
            "item_pos_x",
            "item_pos_y",
            "item_vel_x",
            "item_vel_y",
        )
    }

    # Preallocated buffers (bytes) that C reads/writes.
    seed_bytes = np.empty((min(args.chunk, num_records), seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((min(args.chunk, num_records), input_stride), dtype=np.uint8)
    input_bytes = np.empty((min(args.chunk, num_records), input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((min(args.chunk, num_records), compare_stride), dtype=np.uint8)

    # Views for vectorized comparisons.
    out_compare_view = out_compare_bytes.view(COMPARE_V0_DTYPE).reshape(-1)

    # Iterate in chunks, resizing the handle buffers as needed by re-init.
    offset = 0
    while offset < num_records:
        chunk_n = min(args.chunk, num_records - offset)
        if chunk_n != seed_bytes.shape[0]:
            # Re-init for the last partial chunk (keeps binding simple).
            handle = binding.init(batch_size=chunk_n, num_players=num_players)
            seed_bytes = np.empty((chunk_n, seed_stride), dtype=np.uint8)
            prev_input_bytes = np.empty((chunk_n, input_stride), dtype=np.uint8)
            input_bytes = np.empty((chunk_n, input_stride), dtype=np.uint8)
            out_compare_bytes = np.empty((chunk_n, compare_stride), dtype=np.uint8)
            out_compare_view = out_compare_bytes.view(COMPARE_V0_DTYPE).reshape(-1)

        chunk = samples[offset : offset + chunk_n]

        # Pack seed/input into byte buffers.
        # Structured dtypes don't always permit a zero-copy uint8 view depending on layout;
        # use a contiguous bytes pack per chunk (still fast and keeps Python gameplay-free).
        seed_bytes[:] = np.frombuffer(chunk["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            chunk_n, seed_stride
        )
        prev_input_bytes[:] = np.frombuffer(
            chunk["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(chunk_n, input_stride)
        input_bytes[:] = np.frombuffer(chunk["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            chunk_n, input_stride
        )

        binding.reseed_seed_v0(handle, seed_bytes)
        binding.step_input_v0(handle, prev_input_bytes, input_bytes)
        binding.write_compare_v0(handle, out_compare_bytes)

        ref = chunk["ref_t1"]

        # Compare discretes for active players only.
        active = slice(0, num_players)

        mismatches["action_id"] += int((out_compare_view["action_id"][:, active] != ref["action_id"][:, active]).sum())
        mismatches["action_frame"] += int(
            (out_compare_view["action_frame"][:, active] != ref["action_frame"][:, active]).sum()
        )
        mismatches["on_ground"] += int((out_compare_view["on_ground"][:, active] != ref["on_ground"][:, active]).sum())
        mismatches["facing"] += int((out_compare_view["facing"][:, active] != ref["facing"][:, active]).sum())
        mismatches["stocks"] += int((out_compare_view["stocks"][:, active] != ref["stocks"][:, active]).sum())
        mismatches["jumps_left"] += int((out_compare_view["jumps_left"][:, active] != ref["jumps_left"][:, active]).sum())
        mismatches["is_dead"] += int((out_compare_view["is_dead"][:, active] != ref["is_dead"][:, active]).sum())
        mismatches["hitlag"] += int((out_compare_view["hitlag"][:, active] != ref["hitlag"][:, active]).sum())
        mismatches["hitstun"] += int((out_compare_view["hitstun"][:, active] != ref["hitstun"][:, active]).sum())
        mismatches["l_cancel"] += int((out_compare_view["l_cancel"][:, active] != ref["l_cancel"][:, active]).sum())
        mismatches["hurtbox_state"] += int(
            (out_compare_view["hurtbox_state"][:, active] != ref["hurtbox_state"][:, active]).sum()
        )
        mismatches["ground_id"] += int((out_compare_view["ground_id"][:, active] != ref["ground_id"][:, active]).sum())
        mismatches["animation_index"] += int(
            (out_compare_view["animation_index"][:, active] != ref["animation_index"][:, active]).sum()
        )
        mismatches["instance_hit_by"] += int(
            (out_compare_view["instance_hit_by"][:, active] != ref["instance_hit_by"][:, active]).sum()
        )
        mismatches["instance_id"] += int(
            (out_compare_view["instance_id"][:, active] != ref["instance_id"][:, active]).sum()
        )
        mismatches["last_attack_landed"] += int(
            (out_compare_view["last_attack_landed"][:, active] != ref["last_attack_landed"][:, active]).sum()
        )
        mismatches["combo_count"] += int(
            (out_compare_view["combo_count"][:, active] != ref["combo_count"][:, active]).sum()
        )
        mismatches["last_hit_by"] += int(
            (out_compare_view["last_hit_by"][:, active] != ref["last_hit_by"][:, active]).sum()
        )

        # state_flags is (players,5)
        mismatches["state_flags"] += int(
            (out_compare_view["state_flags"][:, active, :] != ref["state_flags"][:, active, :]).sum()
        )

        # Items: compare all 15 slots (global), but only for exists-matched slots for float errors.
        out_items = out_compare_view["items"]
        ref_items = ref["items"]
        mismatches["item_exists"] += int((out_items["exists"] != ref_items["exists"]).sum())
        mismatches["item_type"] += int((out_items["type"] != ref_items["type"]).sum())
        mismatches["item_state"] += int((out_items["state"] != ref_items["state"]).sum())
        mismatches["item_owner"] += int((out_items["owner"] != ref_items["owner"]).sum())
        mismatches["item_instance_id"] += int((out_items["instance_id"] != ref_items["instance_id"]).sum())

        for k in float_err:
            if k.startswith("item_"):
                name = k.replace("item_", "")
                mask = ref["items"]["exists"].astype(bool)
                err = out_items[name].astype(np.float32) - ref_items[name].astype(np.float32)
                float_err[k].append(err[mask].reshape(-1))
            else:
                err = out_compare_view[k][:, active].astype(np.float32) - ref[k][:, active].astype(np.float32)
                float_err[k].append(err.reshape(-1))

        total_records += chunk_n
        total_player_frames += chunk_n * num_players
        offset += chunk_n

    total_state_flags = total_player_frames * 5
    total_item_slots = total_records * max_items

    print(
        f"Records: {num_records}  Players/scored per record: {num_players}  Total player-frames: {total_player_frames}"
    )
    for k, v in mismatches.items():
        if k == "state_flags":
            denom = total_state_flags
        elif k.startswith("item_"):
            denom = total_item_slots
        else:
            denom = total_player_frames
        print(f"mismatch.{k}: {v} / {denom} ({v/denom:.6f})")
    for k, pieces in float_err.items():
        m = _float_metrics(np.concatenate(pieces, axis=0) if pieces else np.array([], dtype=np.float32))
        print(f"err.{k}: mae={m.mae:.6f} p95={m.p95:.6f} max={m.mx:.6f}")


if __name__ == "__main__":
    main()
