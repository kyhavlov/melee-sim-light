from __future__ import annotations

import argparse

import numpy as np

from tools.eval.dataset import SAMPLE_DTYPE, write_dataset


def _rand_inputs(rng: np.random.Generator, n: int) -> np.ndarray:
    x = np.zeros(n, dtype=SAMPLE_DTYPE)
    # Some plausible-ish metadata defaults
    x["seed_t"]["stage_id"] = 0  # FD placeholder
    x["seed_t"]["match_damage_ratio"] = np.float32(1.0)
    x["seed_t"]["num_players"] = 2
    x["seed_t"]["is_teams"] = 0
    x["seed_t"]["frame_id"] = np.arange(n, dtype=np.int32)
    x["seed_t"]["frame_pre_random_seed"] = rng.integers(0, 2**32, size=n, dtype=np.uint32)

    # Randomize players 0..1; leave 2..3 zeroed.
    x["seed_t"]["char_id"][:, 0] = 0x02  # Fox placeholder
    x["seed_t"]["char_id"][:, 1] = 0x03  # Falco placeholder
    x["seed_t"]["attack_ratio"][:, :2] = np.float32(1.0)
    x["seed_t"]["defense_ratio"][:, :2] = np.float32(1.0)

    x["seed_t"]["pos_x"][:, :2] = rng.normal(0.0, 20.0, size=(n, 2)).astype(np.float32)
    x["seed_t"]["pos_y"][:, :2] = rng.normal(0.0, 5.0, size=(n, 2)).astype(np.float32)
    x["seed_t"]["speed_air_x_self"][:, :2] = rng.normal(0.0, 1.0, size=(n, 2)).astype(np.float32)
    x["seed_t"]["speed_ground_x_self"][:, :2] = rng.normal(0.0, 1.0, size=(n, 2)).astype(np.float32)
    x["seed_t"]["speed_y_self"][:, :2] = rng.normal(0.0, 1.0, size=(n, 2)).astype(np.float32)
    x["seed_t"]["speed_x_attack"][:, :2] = rng.normal(0.0, 1.0, size=(n, 2)).astype(np.float32)
    x["seed_t"]["speed_y_attack"][:, :2] = rng.normal(0.0, 1.0, size=(n, 2)).astype(np.float32)
    x["seed_t"]["fighter_scale_y"][:, :2] = np.float32(1.0)

    x["seed_t"]["facing"][:, :2] = rng.integers(0, 2, size=(n, 2), dtype=np.uint8)
    x["seed_t"]["on_ground"][:, :2] = rng.integers(0, 2, size=(n, 2), dtype=np.uint8)

    x["seed_t"]["action_id"][:, :2] = rng.integers(0, 0x18F, size=(n, 2), dtype=np.uint16)
    x["seed_t"]["action_frame"][:, :2] = rng.integers(0, 60, size=(n, 2), dtype=np.int16)
    x["seed_t"]["jumps_left"][:, :2] = rng.integers(0, 6, size=(n, 2), dtype=np.uint8)
    x["seed_t"]["stocks"][:, :2] = rng.integers(1, 5, size=(n, 2), dtype=np.uint8)

    x["seed_t"]["percent"][:, :2] = rng.uniform(0.0, 120.0, size=(n, 2)).astype(np.float32)
    x["seed_t"]["shield_hp"][:, :2] = rng.uniform(0.0, 60.0, size=(n, 2)).astype(np.float32)
    x["seed_t"]["hitlag"][:, :2] = rng.integers(0, 10, size=(n, 2), dtype=np.uint16)
    x["seed_t"]["hitstun"][:, :2] = rng.integers(0, 60, size=(n, 2), dtype=np.uint16)
    x["seed_t"]["l_cancel"][:, :2] = rng.integers(0, 3, size=(n, 2), dtype=np.uint8)
    x["seed_t"]["hurtbox_state"][:, :2] = rng.integers(0, 3, size=(n, 2), dtype=np.uint8)
    x["seed_t"]["ground_id"][:, :2] = rng.integers(0, 512, size=(n, 2), dtype=np.uint16)
    x["seed_t"]["animation_index"][:, :2] = rng.integers(0, 2048, size=(n, 2), dtype=np.uint32)
    x["seed_t"]["instance_hit_by"][:, :2] = rng.integers(0, 4096, size=(n, 2), dtype=np.uint16)
    x["seed_t"]["instance_id"][:, :2] = rng.integers(0, 4096, size=(n, 2), dtype=np.uint16)
    x["seed_t"]["last_attack_landed"][:, :2] = rng.integers(0, 256, size=(n, 2), dtype=np.uint8)
    x["seed_t"]["combo_count"][:, :2] = rng.integers(0, 32, size=(n, 2), dtype=np.uint8)
    x["seed_t"]["last_hit_by"][:, :2] = rng.integers(0, 4, size=(n, 2), dtype=np.uint8)
    x["seed_t"]["state_flags"][:, :2, :] = rng.integers(0, 256, size=(n, 2, 5), dtype=np.uint8)

    # Random items (sparse)
    exists = rng.random(size=(n, 15)) < 0.1
    x["seed_t"]["items"]["exists"] = exists.astype(np.uint8)
    x["seed_t"]["items"]["type"] = rng.integers(0, 0xEC, size=(n, 15), dtype=np.uint16)
    x["seed_t"]["items"]["state"] = rng.integers(0, 16, size=(n, 15), dtype=np.uint8)
    x["seed_t"]["items"]["owner"] = rng.integers(-1, 3, size=(n, 15), dtype=np.int8)
    x["seed_t"]["items"]["instance_id"] = rng.integers(0, 4096, size=(n, 15), dtype=np.uint16)
    x["seed_t"]["items"]["direction"] = rng.choice([-1.0, 1.0], size=(n, 15)).astype(np.float32)
    x["seed_t"]["items"]["vel_x"] = rng.normal(0.0, 5.0, size=(n, 15)).astype(np.float32)
    x["seed_t"]["items"]["vel_y"] = rng.normal(0.0, 5.0, size=(n, 15)).astype(np.float32)
    x["seed_t"]["items"]["pos_x"] = rng.normal(0.0, 50.0, size=(n, 15)).astype(np.float32)
    x["seed_t"]["items"]["pos_y"] = rng.normal(0.0, 20.0, size=(n, 15)).astype(np.float32)
    x["seed_t"]["items"]["damage"] = rng.integers(0, 256, size=(n, 15), dtype=np.uint16)
    x["seed_t"]["items"]["timer"] = rng.normal(0.0, 60.0, size=(n, 15)).astype(np.float32)
    x["seed_t"]["items"]["spawn_id"] = rng.integers(0, 2**32, size=(n, 15), dtype=np.uint32)
    x["seed_t"]["items"]["misc0"] = rng.integers(0, 256, size=(n, 15), dtype=np.uint8)
    x["seed_t"]["items"]["misc1"] = rng.integers(0, 256, size=(n, 15), dtype=np.uint8)
    x["seed_t"]["items"]["misc2"] = rng.integers(0, 256, size=(n, 15), dtype=np.uint8)
    x["seed_t"]["items"]["misc3"] = rng.integers(0, 256, size=(n, 15), dtype=np.uint8)

    # Random inputs (raw-ish)
    for field in ("prev_input_t", "input_t"):
        x[field]["p"]["buttons"][:, :2] = rng.integers(0, 1 << 10, size=(n, 2), dtype=np.uint16)
        x[field]["p"]["main_x"][:, :2] = rng.integers(-80, 81, size=(n, 2), dtype=np.int8)
        x[field]["p"]["main_y"][:, :2] = rng.integers(-80, 81, size=(n, 2), dtype=np.int8)
        x[field]["p"]["c_x"][:, :2] = rng.integers(-80, 81, size=(n, 2), dtype=np.int8)
        x[field]["p"]["c_y"][:, :2] = rng.integers(-80, 81, size=(n, 2), dtype=np.int8)
        x[field]["p"]["l"][:, :2] = rng.integers(0, 256, size=(n, 2), dtype=np.uint8)
        x[field]["p"]["r"][:, :2] = rng.integers(0, 256, size=(n, 2), dtype=np.uint8)

    # Empty sim expects ref_t1 == seeded state (since step is a no-op).
    x["ref_t1"]["stage_id"] = x["seed_t"]["stage_id"]
    x["ref_t1"]["num_players"] = x["seed_t"]["num_players"]
    x["ref_t1"]["is_teams"] = x["seed_t"]["is_teams"]
    x["ref_t1"]["team_id"] = x["seed_t"]["team_id"]
    x["ref_t1"]["char_id"] = x["seed_t"]["char_id"]
    x["ref_t1"]["frame_id"] = x["seed_t"]["frame_id"]
    x["ref_t1"]["frame_pre_random_seed"] = x["seed_t"]["frame_pre_random_seed"]

    for name in (
        "pos_x",
        "pos_y",
        "speed_air_x_self",
        "speed_ground_x_self",
        "speed_y_self",
        "speed_x_attack",
        "speed_y_attack",
        "facing",
        "on_ground",
        "action_id",
        "action_frame",
        "jumps_left",
        "stocks",
        "percent",
        "shield_hp",
        "hitlag",
        "hitstun",
        "l_cancel",
        "hurtbox_state",
        "ground_id",
        "animation_index",
        "instance_hit_by",
        "instance_id",
        "last_attack_landed",
        "combo_count",
        "last_hit_by",
        "state_flags",
    ):
        x["ref_t1"][name] = x["seed_t"][name]

    x["ref_t1"]["is_dead"] = (x["seed_t"]["stocks"] == 0).astype(np.uint8)
    x["ref_t1"]["items"] = x["seed_t"]["items"]

    return x


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--records", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    samples = _rand_inputs(rng, args.records)
    write_dataset(args.out, num_players=2, samples=samples)
    print(f"Wrote {args.records} records to {args.out}")


if __name__ == "__main__":
    main()
