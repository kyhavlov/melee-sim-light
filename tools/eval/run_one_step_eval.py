from __future__ import annotations

import argparse
import importlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset


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


@dataclass
class EvalSummary:
    total_records: int
    total_player_frames: int
    total_state_flags: int
    total_item_slots: int
    mismatches: dict[str, int]
    float_norm_sum: float
    float_norm_count: int


class Reporter:
    def __init__(self, out_path: Path | None = None) -> None:
        self._fh = None
        if out_path is not None:
            out_path.parent.mkdir(parents=True, exist_ok=True)
            self._fh = out_path.open("w", encoding="utf-8")

    def print(self, *args) -> None:
        line = " ".join(str(a) for a in args)
        print(line)
        if self._fh is not None:
            self._fh.write(line + "\n")

    def close(self) -> None:
        if self._fh is not None:
            self._fh.close()
            self._fh = None


def _iter_mismatch_pairs(diff: np.ndarray):
    # `np.argwhere` returns one row per mismatch with shape (ndim,).
    # We treat axis-0 as the record index, and preserve the remaining axes.
    for row in np.argwhere(diff):
        r = int(row[0])
        subidx = tuple(int(x) for x in row[1:])
        yield r, subidx


def _fmt_subidx(field: str, subidx: tuple[int, ...]) -> str:
    if not subidx:
        return ""
    if field == "state_flags":
        if len(subidx) >= 2:
            return f"p={subidx[0]} byte={subidx[1]}"
        return f"p={subidx[0]}"
    if field.startswith("item_"):
        if len(subidx) >= 2:
            return f"p={subidx[0]} slot={subidx[1]}"
        return f"slot={subidx[0]}"
    if len(subidx) == 1:
        return f"p={subidx[0]}"
    inner = ",".join(str(x) for x in subidx)
    return f"idx=({inner})"


def _safe_scalar(a, idx: tuple[int, ...]) -> str:
    try:
        v = a[idx]
    except Exception:
        return "<?>"
    try:
        return str(int(v))
    except Exception:
        try:
            return str(float(v))
        except Exception:
            return str(v)


def _denom_for_field(
    field: str,
    *,
    total_player_frames: int,
    total_state_flags: int,
    total_item_slots: int,
) -> int:
    if field == "state_flags":
        return total_state_flags
    if field.startswith("item_"):
        return total_item_slots
    return total_player_frames


def _discrete_exact_pct(summary: EvalSummary) -> float:
    total_checks = 0
    total_mismatches = 0
    for k, v in summary.mismatches.items():
        denom = _denom_for_field(
            k,
            total_player_frames=summary.total_player_frames,
            total_state_flags=summary.total_state_flags,
            total_item_slots=summary.total_item_slots,
        )
        total_checks += denom
        total_mismatches += v
    if total_checks == 0:
        return 0.0
    return 100.0 * (1.0 - (total_mismatches / total_checks))


def _discrete_mismatch_total(summary: EvalSummary) -> tuple[int, int]:
    total_checks = 0
    total_mismatches = 0
    for k, v in summary.mismatches.items():
        denom = _denom_for_field(
            k,
            total_player_frames=summary.total_player_frames,
            total_state_flags=summary.total_state_flags,
            total_item_slots=summary.total_item_slots,
        )
        total_checks += denom
        total_mismatches += v
    return total_mismatches, total_checks


def _float_norm_mae_p95(float_err: dict[str, list[np.ndarray]], ref_abs: dict[str, list[np.ndarray]]) -> tuple[float, float, int]:
    total_norm_sum = 0.0
    total_count = 0
    for k, pieces in float_err.items():
        ref_pieces = ref_abs.get(k, [])
        if not pieces or not ref_pieces:
            continue
        err = np.concatenate(pieces, axis=0).astype(np.float32)
        ref_vals = np.concatenate(ref_pieces, axis=0).astype(np.float32)
        if err.size == 0 or ref_vals.size == 0:
            continue
        scale = float(np.quantile(ref_vals, 0.95))
        if scale <= 0.0:
            scale = 1e-6
        total_norm_sum += float(np.abs(err).sum()) / scale
        total_count += int(err.size)
    overall = total_norm_sum / total_count if total_count > 0 else 0.0
    return overall, total_norm_sum, total_count


def evaluate_dataset(
    *,
    dataset_path: Path,
    chunk: int,
    ucf_enabled: bool | None = None,
    ucf_cardinals_1_0_enabled: bool | None = None,
    reporter: Reporter | None = None,
    debug_mismatch: tuple[str, ...] = (),
    debug_limit: int = 10,
) -> EvalSummary:
    if reporter is None:
        reporter = Reporter()

    try:
        ds = read_dataset(str(dataset_path))
    except ValueError as e:
        msg = str(e)
        if "record_size mismatch" in msg:
            reporter.print(f"error: {msg}")
            reporter.print("hint: dataset schema changed; rebuild cached datasets with --force:")
            reporter.print(
                "  uv run python -m tools.slippi.preprocess_suite --suite <suite.json> --datasets-dir <dir> --force"
            )
            raise
        raise
    samples = ds.samples
    num_records = samples.shape[0]
    num_players = int(ds.header["num_players"])
    max_items = int(samples.dtype["seed_t"]["items"].shape[0])

    binding = _load_binding()
    sizes = binding.sizes()

    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    init_kwargs = {"batch_size": min(chunk, num_records), "num_players": num_players}
    if ucf_enabled is not None:
        init_kwargs["ucf_enabled"] = int(bool(ucf_enabled))
    if ucf_cardinals_1_0_enabled is not None:
        init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))

    handle = binding.init(**init_kwargs)

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
    ref_abs = {k: [] for k in float_err}

    # Preallocated buffers (bytes) that C reads/writes.
    seed_bytes = np.empty((min(chunk, num_records), seed_stride), dtype=np.uint8)
    prev_input_bytes = np.empty((min(chunk, num_records), input_stride), dtype=np.uint8)
    input_bytes = np.empty((min(chunk, num_records), input_stride), dtype=np.uint8)
    out_compare_bytes = np.empty((min(chunk, num_records), compare_stride), dtype=np.uint8)

    # Views for vectorized comparisons.
    out_compare_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

    debug_fields = tuple(f for f in debug_mismatch if f in set(mismatches.keys()))
    debug_left: dict[str, int] = {}
    if reporter is not None and debug_fields:
        debug_left = {f: int(debug_limit) for f in debug_fields}
        reporter.print(f"debug: will print first mismatches for {', '.join(debug_fields)} (limit={int(debug_limit)})")
    else:
        # Debug mismatch printing is strictly opt-in: no debug_mismatch flag => no extra work.
        debug_fields = ()

    # Iterate in chunks, resizing the handle buffers as needed by re-init.
    offset = 0
    while offset < num_records:
        chunk_n = min(chunk, num_records - offset)
        if chunk_n != seed_bytes.shape[0]:
            # Re-init for the last partial chunk (keeps binding simple).
            init_kwargs = {"batch_size": chunk_n, "num_players": num_players}
            if ucf_enabled is not None:
                init_kwargs["ucf_enabled"] = int(bool(ucf_enabled))
            if ucf_cardinals_1_0_enabled is not None:
                init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))
            handle = binding.init(**init_kwargs)
            seed_bytes = np.empty((chunk_n, seed_stride), dtype=np.uint8)
            prev_input_bytes = np.empty((chunk_n, input_stride), dtype=np.uint8)
            input_bytes = np.empty((chunk_n, input_stride), dtype=np.uint8)
            out_compare_bytes = np.empty((chunk_n, compare_stride), dtype=np.uint8)
            out_compare_view = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)

        chunk_view = samples[offset : offset + chunk_n]

        # Pack seed/input into byte buffers.
        # Structured dtypes don't always permit a zero-copy uint8 view depending on layout;
        # use a contiguous bytes pack per chunk (still fast and keeps Python gameplay-free).
        seed_bytes[:] = np.frombuffer(chunk_view["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            chunk_n, seed_stride
        )
        prev_input_bytes[:] = np.frombuffer(
            chunk_view["prev_input_t"].tobytes(order="C"), dtype=np.uint8
        ).reshape(chunk_n, input_stride)
        input_bytes[:] = np.frombuffer(chunk_view["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            chunk_n, input_stride
        )

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        seed = chunk_view["seed_t"]
        ref = chunk_view["ref_t1"]

        # Compare discretes for active players only.
        active = slice(0, num_players)

        # Optional debug: print the first N mismatching records for selected discrete fields.
        for field in debug_fields:
            left = debug_left.get(field, 0)
            if left <= 0:
                continue
            if field == "state_flags":
                diff = out_compare_view["state_flags"][:, active, :] != ref["state_flags"][:, active, :]
                if not np.any(diff):
                    continue
                for r, subidx in _iter_mismatch_pairs(diff):
                    left = debug_left.get(field, 0)
                    if left <= 0:
                        break
                    gi = int(offset + r)
                    idx_s = _fmt_subidx(field, subidx)
                    idx = (r,) + subidx
                    reporter.print(
                        f"debug.mismatch.{field}: dataset={dataset_path.name} record={gi} seed_frame={int(seed['frame_id'][r])} ref_frame={int(ref['frame_id'][r])} {idx_s} seed={_safe_scalar(seed['state_flags'], idx)} out={_safe_scalar(out_compare_view['state_flags'], idx)} ref={_safe_scalar(ref['state_flags'], idx)}"
                    )
                    debug_left[field] = left - 1
                continue
            if field.startswith("item_"):
                out_items = out_compare_view["items"]
                ref_items = ref["items"]
                seed_items = seed["items"]

                if field == "item_exists":
                    diff = out_items["exists"] != ref_items["exists"]
                elif field == "item_type":
                    diff = out_items["type"] != ref_items["type"]
                elif field == "item_state":
                    diff = out_items["state"] != ref_items["state"]
                elif field == "item_owner":
                    diff = out_items["owner"] != ref_items["owner"]
                elif field == "item_instance_id":
                    diff = out_items["instance_id"] != ref_items["instance_id"]
                else:
                    continue

                if not np.any(diff):
                    continue

                def _fmt_item(it) -> str:
                    return (
                        f"exists={int(it['exists'])}"
                        f" type={int(it['type'])}"
                        f" state={int(it['state'])}"
                        f" owner={int(it['owner'])}"
                        f" iid={int(it['instance_id'])}"
                        f" aid={int(it['attack_id'])}"
                        f" ains={int(it['attack_instance'])}"
                        f" dir={float(it['direction']):.3f}"
                        f" vel=({float(it['vel_x']):.3f},{float(it['vel_y']):.3f})"
                        f" pos=({float(it['pos_x']):.3f},{float(it['pos_y']):.3f})"
                        f" spawn_id={int(it['spawn_id'])}"
                        f" misc=({int(it['misc0'])},{int(it['misc1'])},{int(it['misc2'])},{int(it['misc3'])})"
                    )

                def _reflect_like(seed_it, out_it) -> bool:
                    if int(seed_it["exists"]) == 0 or int(out_it["exists"]) == 0:
                        return False
                    if int(seed_it["type"]) != int(out_it["type"]):
                        return False
                    if int(seed_it["owner"]) == int(out_it["owner"]):
                        return False
                    vx = abs(float(out_it["vel_x"]) + float(seed_it["vel_x"]))
                    vy = abs(float(out_it["vel_y"]) + float(seed_it["vel_y"]))
                    return vx < 1e-3 and vy < 1e-3

                for r, subidx in _iter_mismatch_pairs(diff):
                    left = debug_left.get(field, 0)
                    if left <= 0:
                        break
                    gi = int(offset + r)
                    idx_s = _fmt_subidx(field, subidx)
                    idx = (r,) + subidx
                    slot = int(subidx[-1]) if subidx else -1

                    seed_it = seed_items[idx]
                    out_it = out_items[idx]
                    ref_it = ref_items[idx]

                    # Include dataset name even when called standalone; suite eval prints a header too.
                    reporter.print(
                        f"debug.mismatch.{field}: dataset={dataset_path.name} record={gi} seed_frame={int(seed['frame_id'][r])} ref_frame={int(ref['frame_id'][r])} {idx_s or f'slot={slot}'} reflect_like={int(_reflect_like(seed_it, out_it))}"
                    )
                    reporter.print(
                        "  seed_flags:",
                        f"p0={tuple(int(x) for x in seed['state_flags'][r, 0, :])}",
                        f"p1={tuple(int(x) for x in seed['state_flags'][r, 1, :])}",
                    )
                    p0_2218 = int(seed["state_flags"][r, 0, 0])
                    p0_221b = int(seed["state_flags"][r, 0, 2])
                    p0_221c = int(seed["state_flags"][r, 0, 3])
                    p1_2218 = int(seed["state_flags"][r, 1, 0])
                    p1_221b = int(seed["state_flags"][r, 1, 2])
                    p1_221c = int(seed["state_flags"][r, 1, 3])
                    reporter.print(
                        "  seed_gate:",
                        # Reflect-active bit is 0x10 in fp+0x2218 as packed by Slippi.
                        # refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
                        # refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_CreateReflectHit
                        f"p0=(rf={int((p0_2218 & 0x10) != 0)} ps={int((p0_221c & 0x20) != 0)} sh={int((p0_221b & 0x80) != 0)})",
                        f"p1=(rf={int((p1_2218 & 0x10) != 0)} ps={int((p1_221c & 0x20) != 0)} sh={int((p1_221b & 0x80) != 0)})",
                    )
                    reporter.print(
                        "  seed_p:",
                        f"p0_action_id={int(seed['action_id'][r, 0])}",
                        f"p1_action_id={int(seed['action_id'][r, 1])}",
                        f"p0_pos=({float(seed['pos_x'][r, 0]):.3f},{float(seed['pos_y'][r, 0]):.3f})",
                        f"p1_pos=({float(seed['pos_x'][r, 1]):.3f},{float(seed['pos_y'][r, 1]):.3f})",
                    )
                    reporter.print("  seed:", _fmt_item(seed_it))
                    reporter.print("  out :", _fmt_item(out_it))
                    reporter.print("  ref :", _fmt_item(ref_it))
                    debug_left[field] = left - 1
            else:
                diff = out_compare_view[field][:, active] != ref[field][:, active]
                if not np.any(diff):
                    continue
                for r, subidx in _iter_mismatch_pairs(diff):
                    left = debug_left.get(field, 0)
                    if left <= 0:
                        break
                    gi = int(offset + r)
                    idx_s = _fmt_subidx(field, subidx)
                    if len(subidx) != 1:
                        idx = (r,) + subidx
                        reporter.print(
                            f"debug.mismatch.{field}: dataset={dataset_path.name} record={gi} seed_frame={int(seed['frame_id'][r])} ref_frame={int(ref['frame_id'][r])} {idx_s} seed={_safe_scalar(seed[field], idx)} out={_safe_scalar(out_compare_view[field], idx)} ref={_safe_scalar(ref[field], idx)}"
                        )
                        debug_left[field] = left - 1
                        continue
                    pp = int(subidx[0])
                    reporter.print(
                        f"debug.mismatch.{field}: dataset={dataset_path.name} record={gi} seed_frame={int(seed['frame_id'][r])} ref_frame={int(ref['frame_id'][r])} {idx_s}"
                    )
                    reporter.print(
                        "  seed:",
                        f"action_id={int(seed['action_id'][r, pp])}",
                        f"anim={int(seed['animation_index'][r, pp])}",
                        f"timer={int(seed['match_flow_timer'][r, pp])}",
                        f"af={int(seed['action_frame'][r, pp])}",
                        f"anim_f={float(seed['anim_frame_f32'][r, pp]):.3f}",
                        f"pos=({float(seed['pos_x'][r, pp]):.3f},{float(seed['pos_y'][r, pp]):.3f})",
                        f"stocks={int(seed['stocks'][r, pp])}",
                    )
                    reporter.print(
                        "  out :",
                        f"action_id={int(out_compare_view['action_id'][r, pp])}",
                        f"anim={int(out_compare_view['animation_index'][r, pp])}",
                        f"pos=({float(out_compare_view['pos_x'][r, pp]):.3f},{float(out_compare_view['pos_y'][r, pp]):.3f})",
                        f"stocks={int(out_compare_view['stocks'][r, pp])}",
                        f"is_dead={int(out_compare_view['is_dead'][r, pp])}",
                    )
                    reporter.print(
                        "  ref :",
                        f"action_id={int(ref['action_id'][r, pp])}",
                        f"anim={int(ref['animation_index'][r, pp])}",
                        f"pos=({float(ref['pos_x'][r, pp]):.3f},{float(ref['pos_y'][r, pp]):.3f})",
                        f"stocks={int(ref['stocks'][r, pp])}",
                        f"is_dead={int(ref['is_dead'][r, pp])}",
                    )
                    debug_left[field] = left - 1

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
                ref_abs[k].append(np.abs(ref_items[name].astype(np.float32)[mask].reshape(-1)))
            else:
                err = out_compare_view[k][:, active].astype(np.float32) - ref[k][:, active].astype(np.float32)
                float_err[k].append(err.reshape(-1))
                ref_abs[k].append(np.abs(ref[k][:, active].astype(np.float32)).reshape(-1))

        total_records += chunk_n
        total_player_frames += chunk_n * num_players
        offset += chunk_n

    total_state_flags = total_player_frames * 5
    total_item_slots = total_records * max_items

    reporter.print(
        f"Records: {num_records}  Players/scored per record: {num_players}  Total player-frames: {total_player_frames}"
    )
    for k, v in mismatches.items():
        denom = _denom_for_field(
            k,
            total_player_frames=total_player_frames,
            total_state_flags=total_state_flags,
            total_item_slots=total_item_slots,
        )
        reporter.print(f"mismatch.{k}: {v} / {denom} ({v/denom:.6f})")
    for k, pieces in float_err.items():
        m = _float_metrics(np.concatenate(pieces, axis=0) if pieces else np.array([], dtype=np.float32))
        reporter.print(f"err.{k}: mae={m.mae:.6f} p95={m.p95:.6f} max={m.mx:.6f}")

    overall_float_norm, float_norm_sum, float_norm_count = _float_norm_mae_p95(float_err, ref_abs)

    summary = EvalSummary(
        total_records=total_records,
        total_player_frames=total_player_frames,
        total_state_flags=total_state_flags,
        total_item_slots=total_item_slots,
        mismatches=mismatches,
        float_norm_sum=float_norm_sum,
        float_norm_count=float_norm_count,
    )
    mismatches_total, checks_total = _discrete_mismatch_total(summary)
    reporter.print(f"overall.discrete_mismatch: {mismatches_total} / {checks_total}")
    reporter.print(f"overall.float_norm_mae_p95: {overall_float_norm:.8f}")
    return summary


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", required=True)
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--out", type=Path, default=None, help="optional output path to write the report")
    args = ap.parse_args()

    reporter = Reporter(args.out)
    try:
        evaluate_dataset(dataset_path=Path(args.dataset), chunk=args.chunk, reporter=reporter)
    finally:
        reporter.close()


if __name__ == "__main__":
    main()
