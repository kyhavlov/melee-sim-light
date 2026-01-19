from __future__ import annotations

import argparse
from dataclasses import dataclass

import numpy as np

import pyarrow as pa
from peppi_py import _read_slippi

from tools.eval.dataset import SAMPLE_DTYPE, write_dataset


@dataclass(frozen=True)
class PortStatic:
    team_id: int
    char_id: int  # start character; per-frame character comes from post


def _to_numpy(arr) -> np.ndarray:
    # pyarrow Array.to_numpy defaults to zero_copy_only=True, which can fail depending on chunking/nulls.
    x = arr.to_numpy(zero_copy_only=False)
    if isinstance(x, np.ma.MaskedArray):
        x = x.filled(0)
    if isinstance(x, np.ndarray) and x.dtype.kind == "f":
        x = np.nan_to_num(x, nan=0.0, posinf=0.0, neginf=0.0)
    return x


def _dir_to_facing(direction: np.ndarray) -> np.ndarray:
    # direction is float: -1 (left) or +1 (right); map to 0/1.
    return (direction > 0).astype(np.uint8)


def _airborne_to_on_ground(airborne: np.ndarray | None, n: int) -> np.ndarray:
    if airborne is None:
        return np.zeros(n, dtype=np.uint8)
    # Slippi: 0 grounded, 1 airborne.
    return (airborne == 0).astype(np.uint8)


def _u8_from_float01(x: np.ndarray) -> np.ndarray:
    x = np.clip(x, 0.0, 1.0)
    return np.round(x * 255.0).astype(np.uint8)


def _int8_from_float_axis(x: np.ndarray) -> np.ndarray:
    # Fallback when raw UCF int8 fields are missing: scale processed [-1,1] float.
    x = np.clip(x, -1.0, 1.0)
    return np.round(x * 127.0).astype(np.int8)


def _u16_from_float_frames(x: np.ndarray | None, n: int) -> np.ndarray:
    if x is None:
        return np.zeros(n, dtype=np.uint16)
    x = np.clip(x, 0.0, 65535.0)
    return np.floor(x).astype(np.uint16)


def _u16_from_hitstun_misc(misc_as: np.ndarray | None, n: int) -> np.ndarray:
    # Slippi spec: misc_as is hitstun remaining when in hitstun; otherwise used for other things.
    # For now we clamp-negative-to-0 and floor.
    if misc_as is None:
        return np.zeros(n, dtype=np.uint16)
    x = np.clip(misc_as, 0.0, 65535.0)
    return np.floor(x).astype(np.uint16)


def _i16_from_state_age(state_age: np.ndarray | None, n: int) -> np.ndarray:
    if state_age is None:
        return np.zeros(n, dtype=np.int16)
    # Keep it simple: floor the float (can be fractional in some actions).
    x = np.clip(state_age, -32768.0, 32767.0)
    return np.floor(x).astype(np.int16)


def _finalized_frame_indices(frame_ids: np.ndarray) -> np.ndarray:
    """
    Slippi rollback can include multiple snapshots for the same frame number.
    For lite-sim evaluation we want the finalized frame sequence: keep the
    *last* occurrence of each frame id.
    """
    frame_ids = np.asarray(frame_ids)
    seen: set[int] = set()
    keep_rev: list[int] = []
    for i in range(len(frame_ids) - 1, -1, -1):
        fid = int(frame_ids[i])
        if fid in seen:
            continue
        seen.add(fid)
        keep_rev.append(i)
    keep_rev.reverse()
    return np.asarray(keep_rev, dtype=np.int32)


def _port_name(port_1based: int) -> str:
    if port_1based < 1 or port_1based > 4:
        raise ValueError(f"port must be in 1..4, got {port_1based}")
    return f"P{port_1based}"


def _team_id_from_start_player(p: dict) -> int:
    t = p.get("team")
    if t is None:
        return 0
    # Slippi start payload commonly encodes team as {color: int, ...}
    c = t.get("color")
    return int(c) if c is not None else 0


def _fill_items_fixed(frames: pa.StructArray, n_frames: int) -> np.ndarray:
    """
    Convert Slippi frame items (list<struct<...>>) into a fixed-length [n_frames, 15]
    array matching the dataset's ITEM dtype, with a stable ordering.

    Ordering: sort by (instance_id, spawn_id/id, type).
    """
    out = np.zeros((n_frames, 15), dtype=SAMPLE_DTYPE["seed_t"]["items"].base)
    out["owner"] = np.int8(-1)

    if "item" not in {f.name for f in frames.type}:
        return out

    items_list = frames.field("item")

    # Offline preprocessing: simplest correct approach is converting to Python lists.
    items_py = items_list.to_pylist()
    for fi, lst in enumerate(items_py):
        if not lst:
            continue
        lst = sorted(lst, key=lambda it: (int(it["instance_id"]), int(it["id"]), int(it["type"])))
        for slot, it in enumerate(lst[:15]):
            out[fi, slot]["exists"] = np.uint8(1)
            out[fi, slot]["state"] = np.uint8(int(it["state"]))
            out[fi, slot]["type"] = np.uint16(int(it["type"]))
            out[fi, slot]["owner"] = np.int8(int(it.get("owner", -1)))
            out[fi, slot]["instance_id"] = np.uint16(int(it["instance_id"]))
            out[fi, slot]["direction"] = np.float32(float(it["direction"]))
            out[fi, slot]["vel_x"] = np.float32(float(it["velocity"]["x"]))
            out[fi, slot]["vel_y"] = np.float32(float(it["velocity"]["y"]))
            out[fi, slot]["pos_x"] = np.float32(float(it["position"]["x"]))
            out[fi, slot]["pos_y"] = np.float32(float(it["position"]["y"]))
            out[fi, slot]["damage"] = np.uint16(int(it["damage"]))
            out[fi, slot]["timer"] = np.float32(float(it["timer"]))
            out[fi, slot]["spawn_id"] = np.uint32(int(it["id"]))
            misc = it.get("misc") or {}
            out[fi, slot]["misc0"] = np.uint8(int(misc.get("0", 0)))
            out[fi, slot]["misc1"] = np.uint8(int(misc.get("1", 0)))
            out[fi, slot]["misc2"] = np.uint8(int(misc.get("2", 0)))
            out[fi, slot]["misc3"] = np.uint8(int(misc.get("3", 0)))

    return out


def write_dataset_from_slp(
    *,
    slp_path: str,
    out_path: str,
    ports: list[int] | None = None,
) -> None:
    """
    Build a dataset from a single .slp by reseeding with post(i-1),
    applying inputs from pre(i), and comparing to post(i).

    ports: optional list of 1-based ports to include (e.g. [1,2]).
    """
    class Args:
        pass

    a = Args()
    a.slp = slp_path
    a.out = out_path
    a.ports = None if ports is None else ",".join(str(p) for p in ports)

    _main_impl(a)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--slp", required=True, help="Path to .slp file")
    ap.add_argument("--out", required=True, help="Output .msl dataset path")
    ap.add_argument(
        "--ports",
        default=None,
        help="Comma-separated 1-based ports to include (e.g. '1,2' for singles). "
        "If omitted, uses all HUMAN ports from game start.",
    )
    args = ap.parse_args()
    _main_impl(args)

def _main_impl(args) -> None:
    game = _read_slippi(args.slp, False)
    frames_all = game.frames
    if frames_all is None or len(frames_all) == 0:
        raise ValueError("Replay has no frames")

    # Decide which source ports to include.
    if args.ports is not None:
        src_ports = [int(x.strip()) for x in args.ports.split(",") if x.strip()]
        if any(p < 1 or p > 4 for p in src_ports):
            raise ValueError(f"--ports must be in 1..4, got {src_ports}")
        src_ports = sorted(src_ports)
    else:
        # Default: use HUMAN ports from game start (common for RL suites).
        players = list(game.start.get("players", []))
        src_ports = []
        for p in players:
            if str(p.get("type")) != "Human":
                continue
            port = str(p.get("port"))
            if not port.startswith("P"):
                continue
            src_ports.append(int(port[1:]))
        src_ports = sorted(src_ports)

    if len(src_ports) not in (2, 4):
        raise ValueError(f"Expected 2 or 4 selected ports, got {src_ports}")

    src_port_names = [_port_name(p) for p in src_ports]
    num_players = len(src_port_names)

    # Map static info by 1-based port. Character may change in-game; per-frame char id comes from post.character.
    static_by_port: dict[int, PortStatic] = {}
    for p in game.start.get("players", []):
        port = str(p.get("port", ""))
        if not port.startswith("P"):
            continue
        port_1based = int(port[1:])
        static_by_port[port_1based] = PortStatic(
            team_id=_team_id_from_start_player(p),
            char_id=int(p.get("character", 0)),
        )

    frame_ids_all = _to_numpy(frames_all.field("id"))
    keep = _finalized_frame_indices(frame_ids_all)
    frames = frames_all.take(pa.array(keep))
    frame_ids = _to_numpy(frames.field("id")).astype(np.int32)
    n_frames = int(len(frames))
    if n_frames < 2:
        raise ValueError("Not enough frames for one-step dataset")

    # Per-frame seed for determinism (global RNG seed recorded by Slippi).
    frame_pre_random_seed = _to_numpy(frames.field("start").field("random_seed")).astype(np.uint32)

    # We build samples for indices i=1..n_frames-1:
    # seed_t  := post(i-1)
    # input_t := pre(i)
    # prev_input_t := pre(i-1)
    # ref_t1  := post(i)
    n_samples = n_frames - 1
    samples = np.zeros(n_samples, dtype=SAMPLE_DTYPE)

    stage_id = int(game.start.get("stage", 0))
    is_teams = int(bool(game.start.get("is_teams", False)))

    # Frame ids and seeds (seed from frame i-1, ref from frame i).
    samples["seed_t"]["frame_id"] = frame_ids[:-1]
    samples["ref_t1"]["frame_id"] = frame_ids[1:]
    samples["seed_t"]["frame_pre_random_seed"] = frame_pre_random_seed[:-1]
    samples["ref_t1"]["frame_pre_random_seed"] = frame_pre_random_seed[1:]

    samples["seed_t"]["stage_id"] = stage_id
    samples["seed_t"]["num_players"] = num_players
    samples["seed_t"]["is_teams"] = is_teams
    samples["ref_t1"]["stage_id"] = stage_id
    samples["ref_t1"]["num_players"] = num_players
    samples["ref_t1"]["is_teams"] = is_teams

    # Static team ids from game start (slot order follows src_ports list).
    for slot, port_1based in enumerate(src_ports):
        st = static_by_port.get(port_1based, PortStatic(team_id=0, char_id=0))
        samples["seed_t"]["team_id"][:, slot] = np.uint8(st.team_id)
        samples["ref_t1"]["team_id"][:, slot] = np.uint8(st.team_id)

    # Fill inputs and per-port post state.
    ports_struct = frames.field("ports")
    available_ports = set(f.name for f in ports_struct.type)
    for slot, port_name in enumerate(src_port_names):
        if port_name not in available_ports:
            raise ValueError(f"Replay missing port {port_name}; available ports: {sorted(available_ports)}")
        leader = ports_struct.field(port_name).field("leader")
        pre = leader.field("pre")
        post = leader.field("post")

        # --- Pre-frame inputs (prev and current)
        pre_buttons_physical = _to_numpy(pre.field("buttons_physical")).astype(np.uint16)
        pre_main_x = _to_numpy(pre.field("raw_analog_x")).astype(np.int8)
        pre_main_y = _to_numpy(pre.field("raw_analog_y")).astype(np.int8)
        pre_c_x = _to_numpy(pre.field("raw_analog_cstick_x")).astype(np.int8)
        pre_c_y = _to_numpy(pre.field("raw_analog_cstick_y")).astype(np.int8)
        pre_l = _u8_from_float01(_to_numpy(pre.field("triggers_physical").field("l")).astype(np.float32))
        pre_r = _u8_from_float01(_to_numpy(pre.field("triggers_physical").field("r")).astype(np.float32))

        # i=0..n_samples-1 corresponds to frame index (i+1) for current input and i for prev.
        samples["prev_input_t"]["p"]["buttons"][:, slot] = pre_buttons_physical[:-1]
        samples["input_t"]["p"]["buttons"][:, slot] = pre_buttons_physical[1:]
        samples["prev_input_t"]["p"]["main_x"][:, slot] = pre_main_x[:-1]
        samples["input_t"]["p"]["main_x"][:, slot] = pre_main_x[1:]
        samples["prev_input_t"]["p"]["main_y"][:, slot] = pre_main_y[:-1]
        samples["input_t"]["p"]["main_y"][:, slot] = pre_main_y[1:]
        samples["prev_input_t"]["p"]["c_x"][:, slot] = pre_c_x[:-1]
        samples["input_t"]["p"]["c_x"][:, slot] = pre_c_x[1:]
        samples["prev_input_t"]["p"]["c_y"][:, slot] = pre_c_y[:-1]
        samples["input_t"]["p"]["c_y"][:, slot] = pre_c_y[1:]
        samples["prev_input_t"]["p"]["l"][:, slot] = pre_l[:-1]
        samples["input_t"]["p"]["l"][:, slot] = pre_l[1:]
        samples["prev_input_t"]["p"]["r"][:, slot] = pre_r[:-1]
        samples["input_t"]["p"]["r"][:, slot] = pre_r[1:]

        # --- Post-frame state (seed/ref)
        post_char = _to_numpy(post.field("character")).astype(np.uint8)
        post_state = _to_numpy(post.field("state")).astype(np.uint16)
        post_pos_x = _to_numpy(post.field("position").field("x")).astype(np.float32)
        post_pos_y = _to_numpy(post.field("position").field("y")).astype(np.float32)
        post_dir = _dir_to_facing(_to_numpy(post.field("direction")).astype(np.float32))
        post_percent = _to_numpy(post.field("percent")).astype(np.float32)
        post_shield = _to_numpy(post.field("shield")).astype(np.float32)
        post_stocks = _to_numpy(post.field("stocks")).astype(np.uint8)
        post_jumps = _to_numpy(post.field("jumps")).astype(np.uint8)
        post_airborne = _to_numpy(post.field("airborne")).astype(np.uint8)
        post_on_ground = _airborne_to_on_ground(post_airborne, n_frames)
        post_hitlag = _u16_from_float_frames(_to_numpy(post.field("hitlag")).astype(np.float32), n_frames)
        post_hitstun = _u16_from_hitstun_misc(_to_numpy(post.field("misc_as")).astype(np.float32), n_frames)
        post_state_age = _i16_from_state_age(_to_numpy(post.field("state_age")).astype(np.float32), n_frames)

        hurtbox_state = _to_numpy(post.field("hurtbox_state")).astype(np.uint8)
        l_cancel = _to_numpy(post.field("l_cancel")).astype(np.uint8)
        ground_id = _to_numpy(post.field("ground")).astype(np.uint16)
        animation_index = _to_numpy(post.field("animation_index")).astype(np.uint32)
        instance_hit_by = _to_numpy(post.field("last_hit_by_instance")).astype(np.uint16)
        instance_id = _to_numpy(post.field("instance_id")).astype(np.uint16)
        last_attack_landed = _to_numpy(post.field("last_attack_landed")).astype(np.uint8)
        combo_count = _to_numpy(post.field("combo_count")).astype(np.uint8)
        last_hit_by = _to_numpy(post.field("last_hit_by")).astype(np.uint8)

        sf = post.field("state_flags")
        state_flags = np.stack(
            [
                _to_numpy(sf.field("0")).astype(np.uint8),
                _to_numpy(sf.field("1")).astype(np.uint8),
                _to_numpy(sf.field("2")).astype(np.uint8),
                _to_numpy(sf.field("3")).astype(np.uint8),
                _to_numpy(sf.field("4")).astype(np.uint8),
            ],
            axis=1,
        )  # [n_frames, 5]

        vel = post.field("velocities")
        speed_air_x_self = _to_numpy(vel.field("self_x_air")).astype(np.float32)
        speed_y_self = _to_numpy(vel.field("self_y")).astype(np.float32)
        speed_x_attack = _to_numpy(vel.field("knockback_x")).astype(np.float32)
        speed_y_attack = _to_numpy(vel.field("knockback_y")).astype(np.float32)
        speed_ground_x_self = _to_numpy(vel.field("self_x_ground")).astype(np.float32)

        # Seed uses post at (i), ref uses post at (i+1).
        samples["seed_t"]["char_id"][:, slot] = post_char[:-1]
        samples["ref_t1"]["char_id"][:, slot] = post_char[1:]

        samples["seed_t"]["action_id"][:, slot] = post_state[:-1]
        samples["ref_t1"]["action_id"][:, slot] = post_state[1:]
        samples["seed_t"]["action_frame"][:, slot] = post_state_age[:-1]
        samples["ref_t1"]["action_frame"][:, slot] = post_state_age[1:]

        samples["seed_t"]["pos_x"][:, slot] = post_pos_x[:-1]
        samples["ref_t1"]["pos_x"][:, slot] = post_pos_x[1:]
        samples["seed_t"]["pos_y"][:, slot] = post_pos_y[:-1]
        samples["ref_t1"]["pos_y"][:, slot] = post_pos_y[1:]
        samples["seed_t"]["speed_air_x_self"][:, slot] = speed_air_x_self[:-1]
        samples["ref_t1"]["speed_air_x_self"][:, slot] = speed_air_x_self[1:]
        samples["seed_t"]["speed_ground_x_self"][:, slot] = speed_ground_x_self[:-1]
        samples["ref_t1"]["speed_ground_x_self"][:, slot] = speed_ground_x_self[1:]
        samples["seed_t"]["speed_y_self"][:, slot] = speed_y_self[:-1]
        samples["ref_t1"]["speed_y_self"][:, slot] = speed_y_self[1:]
        samples["seed_t"]["speed_x_attack"][:, slot] = speed_x_attack[:-1]
        samples["ref_t1"]["speed_x_attack"][:, slot] = speed_x_attack[1:]
        samples["seed_t"]["speed_y_attack"][:, slot] = speed_y_attack[:-1]
        samples["ref_t1"]["speed_y_attack"][:, slot] = speed_y_attack[1:]

        samples["seed_t"]["facing"][:, slot] = post_dir[:-1]
        samples["ref_t1"]["facing"][:, slot] = post_dir[1:]
        samples["seed_t"]["on_ground"][:, slot] = post_on_ground[:-1]
        samples["ref_t1"]["on_ground"][:, slot] = post_on_ground[1:]

        samples["seed_t"]["percent"][:, slot] = post_percent[:-1]
        samples["ref_t1"]["percent"][:, slot] = post_percent[1:]
        samples["seed_t"]["shield_hp"][:, slot] = post_shield[:-1]
        samples["ref_t1"]["shield_hp"][:, slot] = post_shield[1:]
        samples["seed_t"]["stocks"][:, slot] = post_stocks[:-1]
        samples["ref_t1"]["stocks"][:, slot] = post_stocks[1:]
        samples["seed_t"]["jumps_left"][:, slot] = post_jumps[:-1]
        samples["ref_t1"]["jumps_left"][:, slot] = post_jumps[1:]

        samples["seed_t"]["hitlag"][:, slot] = post_hitlag[:-1]
        samples["ref_t1"]["hitlag"][:, slot] = post_hitlag[1:]
        samples["seed_t"]["hitstun"][:, slot] = post_hitstun[:-1]
        samples["ref_t1"]["hitstun"][:, slot] = post_hitstun[1:]

        samples["seed_t"]["l_cancel"][:, slot] = l_cancel[:-1]
        samples["ref_t1"]["l_cancel"][:, slot] = l_cancel[1:]
        samples["seed_t"]["hurtbox_state"][:, slot] = hurtbox_state[:-1]
        samples["ref_t1"]["hurtbox_state"][:, slot] = hurtbox_state[1:]
        samples["seed_t"]["ground_id"][:, slot] = ground_id[:-1]
        samples["ref_t1"]["ground_id"][:, slot] = ground_id[1:]
        samples["seed_t"]["animation_index"][:, slot] = animation_index[:-1]
        samples["ref_t1"]["animation_index"][:, slot] = animation_index[1:]
        samples["seed_t"]["instance_hit_by"][:, slot] = instance_hit_by[:-1]
        samples["ref_t1"]["instance_hit_by"][:, slot] = instance_hit_by[1:]
        samples["seed_t"]["instance_id"][:, slot] = instance_id[:-1]
        samples["ref_t1"]["instance_id"][:, slot] = instance_id[1:]
        samples["seed_t"]["last_attack_landed"][:, slot] = last_attack_landed[:-1]
        samples["ref_t1"]["last_attack_landed"][:, slot] = last_attack_landed[1:]
        samples["seed_t"]["combo_count"][:, slot] = combo_count[:-1]
        samples["ref_t1"]["combo_count"][:, slot] = combo_count[1:]
        samples["seed_t"]["last_hit_by"][:, slot] = last_hit_by[:-1]
        samples["ref_t1"]["last_hit_by"][:, slot] = last_hit_by[1:]

        samples["seed_t"]["state_flags"][:, slot, :] = state_flags[:-1, :]
        samples["ref_t1"]["state_flags"][:, slot, :] = state_flags[1:, :]

    # Items are global per frame.
    items_fixed = _fill_items_fixed(frames, n_frames)
    samples["seed_t"]["items"] = items_fixed[:-1]
    samples["ref_t1"]["items"] = items_fixed[1:]

    # is_dead in compare is derived from stocks in the evaluator too, but fill it here for completeness.
    samples["ref_t1"]["is_dead"] = (samples["ref_t1"]["stocks"] == 0).astype(np.uint8)

    write_dataset(args.out, num_players=num_players, samples=samples)
    print(f"Wrote {n_samples} samples to {args.out} from {args.slp}")


if __name__ == "__main__":
    main()
