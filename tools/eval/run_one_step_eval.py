from __future__ import annotations

import argparse
import heapq
import importlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, Dataset, read_dataset
from tools.eval.validation_profile import ValidationProfile, get_validation_profile, scored_lane_count_for_field


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
    strict_mismatches: dict[str, int]
    ignored_mismatches: dict[str, int]
    profile_name: str
    float_norm_sum: float
    float_norm_count: int


_DISCRETE_FIELDS: tuple[str, ...] = (
    "action_id",
    "action_frame",
    "on_ground",
    "facing",
    "stocks",
    "jumps_left",
    "is_dead",
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
    "item_exists",
    "item_type",
    "item_state",
    "item_owner",
    "item_instance_id",
)

_FLOAT_FIELDS: tuple[str, ...] = (
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


@dataclass
class OneStepEvalRuntime:
    binding: object
    handle: object
    capacity: int
    num_players: int
    source_port0_layout: tuple[int, ...] | None
    seed_stride: int
    input_stride: int
    compare_stride: int
    seed_bytes: np.ndarray
    prev_input_bytes: np.ndarray
    input_bytes: np.ndarray
    out_compare_bytes: np.ndarray
    out_compare_view: np.ndarray

    def close(self) -> None:
        try:
            self.binding.destroy(self.handle)
        except Exception:
            pass


def create_one_step_eval_runtime(
    *,
    batch_size: int,
    num_players: int,
    ucf_enabled: bool | None = None,
    ucf_cardinals_1_0_enabled: bool | None = None,
) -> OneStepEvalRuntime:
    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])
    capacity = max(1, int(batch_size))

    init_kwargs = {"batch_size": capacity, "num_players": int(num_players)}
    if ucf_enabled is not None:
        init_kwargs["ucf_enabled"] = int(bool(ucf_enabled))
    if ucf_cardinals_1_0_enabled is not None:
        init_kwargs["ucf_cardinals_1_0_enabled"] = int(bool(ucf_cardinals_1_0_enabled))

    handle = binding.init(**init_kwargs)
    out_compare_bytes = np.empty((capacity, compare_stride), dtype=np.uint8)
    return OneStepEvalRuntime(
        binding=binding,
        handle=handle,
        capacity=capacity,
        num_players=int(num_players),
        source_port0_layout=None,
        seed_stride=seed_stride,
        input_stride=input_stride,
        compare_stride=compare_stride,
        seed_bytes=np.empty((capacity, seed_stride), dtype=np.uint8),
        prev_input_bytes=np.empty((capacity, input_stride), dtype=np.uint8),
        input_bytes=np.empty((capacity, input_stride), dtype=np.uint8),
        out_compare_bytes=out_compare_bytes,
        out_compare_view=out_compare_bytes.view(COMPARE_DTYPE).reshape(-1),
    )


def _dataset_source_port0_layout(samples: np.ndarray, num_players: int) -> tuple[int, ...]:
    if int(samples.shape[0]) == 0:
        return ()
    return tuple(int(x) for x in samples["seed_t"]["source_port0"][0, : int(num_players)])


class Reporter:
    def __init__(self, out_path: Path | None = None, *, echo: bool = True) -> None:
        self._fh = None
        self._echo = bool(echo)
        if out_path is not None:
            out_path.parent.mkdir(parents=True, exist_ok=True)
            self._fh = out_path.open("w", encoding="utf-8")

    def print(self, *args) -> None:
        line = " ".join(str(a) for a in args)
        if self._echo:
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


def _scored_denom_for_field(
    field: str,
    *,
    total_records: int,
    total_player_frames: int,
    total_state_flags: int,
    total_item_slots: int,
    num_players: int,
    profile: ValidationProfile,
) -> int:
    if field == "state_flags":
        per_record = scored_lane_count_for_field(field, players=num_players, profile=profile)
        return int(total_records) * int(per_record)
    return _denom_for_field(
        field,
        total_player_frames=total_player_frames,
        total_state_flags=total_state_flags,
        total_item_slots=total_item_slots,
    )


def _emit_summary_from_native(
    *,
    native_summary: dict,
    reporter: Reporter,
    validation_profile: ValidationProfile,
    print_profile: bool,
    num_records: int,
    total_records: int,
    total_player_frames: int,
    total_state_flags: int,
    total_item_slots: int,
    num_players: int,
) -> EvalSummary:
    mismatches = {
        field: int(value)
        for field, value in zip(_DISCRETE_FIELDS, native_summary["mismatches"], strict=True)
    }
    strict_mismatches = {
        field: int(value)
        for field, value in zip(_DISCRETE_FIELDS, native_summary["strict_mismatches"], strict=True)
    }
    ignored_mismatches = {lane.label: 0 for lane in validation_profile.ignored_lanes}
    for lane in validation_profile.ignored_lanes:
        if lane.field == "state_flags" and lane.subindex == 4 and lane.bitmask == 0x80:
            ignored_mismatches[lane.label] = int(native_summary["ignored_state_flags_4_0x80"])
    float_metrics = native_summary["float_metrics"]
    float_norm_sum = float(native_summary["float_norm_sum"])
    float_norm_count = int(native_summary["float_norm_count"])
    overall_float_norm = float_norm_sum / float_norm_count if float_norm_count > 0 else 0.0

    if print_profile:
        reporter.print(f"validation.profile: {validation_profile.name}")
        for lane in validation_profile.ignored_lanes:
            reporter.print(
                f"validation.profile.ignored: {lane.label} reason={lane.reason} exception={lane.exception}"
            )
    reporter.print(
        f"Records: {num_records}  Players/scored per record: {num_players}  Total player-frames: {total_player_frames}"
    )
    for k, v in mismatches.items():
        denom = _scored_denom_for_field(
            k,
            total_records=total_records,
            total_player_frames=total_player_frames,
            total_state_flags=total_state_flags,
            total_item_slots=total_item_slots,
            num_players=num_players,
            profile=validation_profile,
        )
        reporter.print(f"mismatch.{k}: {v} / {denom} ({v/denom:.6f})")
    for k, v in ignored_mismatches.items():
        denom = total_player_frames if k.startswith("state_flags[") else 0
        if denom > 0:
            reporter.print(f"ignored_mismatch.{k}: {v} / {denom} ({v/denom:.6f})")
    for k in _FLOAT_FIELDS:
        m = float_metrics[k]
        reporter.print(
            f"err.{k}: mae={float(m['mae']):.6f} p95={float(m['p95']):.6f} max={float(m['max']):.6f}"
        )

    summary = EvalSummary(
        total_records=total_records,
        total_player_frames=total_player_frames,
        total_state_flags=total_state_flags,
        total_item_slots=total_item_slots,
        mismatches=mismatches,
        strict_mismatches=strict_mismatches,
        ignored_mismatches=ignored_mismatches,
        profile_name=validation_profile.name,
        float_norm_sum=float_norm_sum,
        float_norm_count=float_norm_count,
    )
    mismatches_total, checks_total = _discrete_mismatch_total(summary, profile=validation_profile)
    strict_total, strict_checks = _strict_discrete_mismatch_total(summary)
    ignored_total = sum(ignored_mismatches.values())
    reporter.print(f"overall.discrete_mismatch: {mismatches_total} / {checks_total}")
    reporter.print(f"overall.strict_discrete_mismatch: {strict_total} / {strict_checks}")
    reporter.print(f"overall.ignored_discrete_mismatch: {ignored_total}")
    reporter.print(f"overall.float_norm_mae_p95: {overall_float_norm:.8f}")
    return summary


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


def _discrete_mismatch_total(summary: EvalSummary, *, profile: ValidationProfile | None = None) -> tuple[int, int]:
    profile = get_validation_profile(summary.profile_name) if profile is None else profile
    num_players = int(summary.total_player_frames // summary.total_records) if summary.total_records > 0 else 0
    total_checks = 0
    total_mismatches = 0
    for k, v in summary.mismatches.items():
        denom = _scored_denom_for_field(
            k,
            total_records=summary.total_records,
            total_player_frames=summary.total_player_frames,
            total_state_flags=summary.total_state_flags,
            total_item_slots=summary.total_item_slots,
            num_players=num_players,
            profile=profile,
        )
        total_checks += denom
        total_mismatches += v
    return total_mismatches, total_checks


def _strict_discrete_mismatch_total(summary: EvalSummary) -> tuple[int, int]:
    total_checks = 0
    total_mismatches = 0
    for k, v in summary.strict_mismatches.items():
        denom = _denom_for_field(
            k,
            total_player_frames=summary.total_player_frames,
            total_state_flags=summary.total_state_flags,
            total_item_slots=summary.total_item_slots,
        )
        total_checks += denom
        total_mismatches += v
    return total_mismatches, total_checks


def evaluate_dataset(
    *,
    dataset_path: Path,
    chunk: int,
    dataset: Dataset | None = None,
    runtime: OneStepEvalRuntime | None = None,
    profile: str | ValidationProfile | None = None,
    ucf_enabled: bool | None = None,
    ucf_cardinals_1_0_enabled: bool | None = None,
    reporter: Reporter | None = None,
    print_profile: bool = True,
    debug_mismatch: tuple[str, ...] = (),
    debug_limit: int = 10,
    debug_float: tuple[str, ...] = (),
    debug_float_limit: int = 10,
) -> EvalSummary:
    if reporter is None:
        reporter = Reporter()
    validation_profile = get_validation_profile(profile)
    chunk = max(1, int(chunk))

    if dataset is None:
        ds = read_dataset(str(dataset_path))
    else:
        ds = dataset
    samples = ds.samples
    num_records = samples.shape[0]
    num_players = int(ds.header["num_players"])
    max_items = int(samples.dtype["seed_t"]["items"].shape[0])
    source_port0_layout = _dataset_source_port0_layout(samples, num_players)

    owns_runtime = runtime is None
    if runtime is None:
        runtime = create_one_step_eval_runtime(
            batch_size=min(max(1, chunk), max(1, num_records)),
            num_players=num_players,
            ucf_enabled=ucf_enabled,
            ucf_cardinals_1_0_enabled=ucf_cardinals_1_0_enabled,
        )
    elif runtime.capacity < min(max(1, chunk), max(1, num_records)):
        raise ValueError(
            f"shared one-step runtime capacity {runtime.capacity} is too small for "
            f"chunk={chunk} records={num_records}"
        )
    elif runtime.num_players != num_players:
        raise ValueError(
            f"shared one-step runtime num_players={runtime.num_players} does not match "
            f"dataset num_players={num_players}"
        )
    elif runtime.source_port0_layout is None:
        runtime.source_port0_layout = source_port0_layout
    elif runtime.source_port0_layout != source_port0_layout:
        raise ValueError(
            "shared one-step runtime source_port0 layout "
            f"{runtime.source_port0_layout} does not match dataset layout {source_port0_layout}; "
            "create a fresh runtime for a different replay port layout"
        )

    binding = runtime.binding
    handle = runtime.handle
    seed_stride = runtime.seed_stride
    input_stride = runtime.input_stride
    compare_stride = runtime.compare_stride

    total_records = 0
    total_player_frames = 0
    # Preallocated buffers (bytes) that C reads/writes.
    seed_bytes = runtime.seed_bytes
    prev_input_bytes = runtime.prev_input_bytes
    input_bytes = runtime.input_bytes
    out_compare_bytes = runtime.out_compare_bytes
    # Views for vectorized comparisons.
    out_compare_view_full = runtime.out_compare_view
    sample_stride = int(samples.dtype.itemsize)
    samples_u8 = samples.view(np.uint8).reshape(num_records, sample_stride)
    seed_off = int(samples.dtype.fields["seed_t"][1])
    prev_input_off = int(samples.dtype.fields["prev_input_t"][1])
    input_off = int(samples.dtype.fields["input_t"][1])

    if not debug_mismatch and not debug_float:
        native_summary = binding.one_step_eval_samples(
            handle,
            samples_u8,
            num_players,
            int(validation_profile.name == "rl1_gameplay"),
        )
        if owns_runtime and runtime is not None:
            runtime.close()
        total_records = num_records
        total_player_frames = total_records * num_players
        return _emit_summary_from_native(
            native_summary=native_summary,
            reporter=reporter,
            validation_profile=validation_profile,
            print_profile=print_profile,
            num_records=num_records,
            total_records=total_records,
            total_player_frames=total_player_frames,
            total_state_flags=total_player_frames * 5,
            total_item_slots=total_records * max_items,
            num_players=num_players,
        )

    summary_handle = binding.one_step_summary_create(
        num_records,
        num_players,
        int(validation_profile.name == "rl1_gameplay"),
    )

    debug_fields: tuple[str, ...] = ()
    debug_left: dict[str, int] = {}
    if reporter is not None and debug_mismatch:
        valid_discrete = set(_DISCRETE_FIELDS)
        debug_fields = tuple(f for f in debug_mismatch if f in valid_discrete)
        if debug_fields:
            debug_left = {f: int(debug_limit) for f in debug_fields}
            reporter.print(
                f"debug: will print first mismatches for {', '.join(debug_fields)} (limit={int(debug_limit)})"
            )

    debug_float_fields: tuple[str, ...] = ()
    debug_float_heaps: dict[
        str, list[tuple[float, int, int, int, int, float, float, int, int, int, int, int]]
    ] = {}
    debug_float_limit = int(debug_float_limit)
    if reporter is not None and debug_float and debug_float_limit > 0:
        valid_float = set(_FLOAT_FIELDS)
        debug_float_fields = tuple(f for f in debug_float if f in valid_float and not f.startswith("item_"))
        if debug_float_fields:
            debug_float_heaps = {f: [] for f in debug_float_fields}
            reporter.print(
                f"debug: will print top abs float errors for {', '.join(debug_float_fields)} (limit={debug_float_limit})"
            )

    # Iterate in chunks. The C runtime batch size is fixed, so shared suite runtimes pad
    # tail lanes with a valid sample and ignore them in Python-side comparisons.
    offset = 0
    while offset < num_records:
        chunk_n = min(chunk, num_records - offset)

        chunk_view = samples[offset : offset + chunk_n]
        chunk_u8 = samples_u8[offset : offset + chunk_n]

        # Copy seed/input field bytes directly from the AoS dataset buffer. This avoids the
        # per-chunk `.tobytes()` allocation path while preserving the C runtime byte ABI.
        seed_bytes[:chunk_n] = chunk_u8[:, seed_off : seed_off + seed_stride]
        prev_input_bytes[:chunk_n] = chunk_u8[:, prev_input_off : prev_input_off + input_stride]
        input_bytes[:chunk_n] = chunk_u8[:, input_off : input_off + input_stride]
        if chunk_n < runtime.capacity:
            seed_bytes[chunk_n : runtime.capacity] = seed_bytes[0]
            prev_input_bytes[chunk_n : runtime.capacity] = prev_input_bytes[0]
            input_bytes[chunk_n : runtime.capacity] = input_bytes[0]

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)
        out_compare_view = out_compare_view_full[:chunk_n]
        binding.one_step_summary_accumulate(summary_handle, out_compare_bytes[:chunk_n], chunk_u8)

        seed = chunk_view["seed_t"]
        ref = chunk_view["ref_t1"]

        # Compare discretes for active players only.
        active = slice(0, num_players)

        # Optional debug: track top-N absolute float errors with minimal context.
        if debug_float_fields:
            for field in debug_float_fields:
                abs_err = np.abs(
                    out_compare_view[field][:, active].astype(np.float32)
                    - ref[field][:, active].astype(np.float32)
                )
                flat = abs_err.reshape(-1)
                if flat.size == 0:
                    continue
                k = min(debug_float_limit, int(flat.size))
                topk = np.argpartition(flat, -k)[-k:]
                heap = debug_float_heaps.get(field)
                if heap is None:
                    continue
                for j in topk:
                    ae = float(flat[int(j)])
                    r = int(j) // num_players
                    pp = int(j) % num_players
                    gi = int(offset + r)
                    seed_frame = int(seed["frame_id"][r])
                    ref_frame = int(ref["frame_id"][r])
                    out_v = float(out_compare_view[field][r, pp])
                    ref_v = float(ref[field][r, pp])
                    seed_a = int(seed["action_id"][r, pp])
                    out_a = int(out_compare_view["action_id"][r, pp])
                    ref_a = int(ref["action_id"][r, pp])
                    out_af = int(out_compare_view["action_frame"][r, pp])
                    ref_af = int(ref["action_frame"][r, pp])
                    entry = (ae, gi, seed_frame, ref_frame, pp, out_v, ref_v, seed_a, out_a, ref_a, out_af, ref_af)
                    if len(heap) < debug_float_limit:
                        heapq.heappush(heap, entry)
                    else:
                        if ae > heap[0][0]:
                            heapq.heapreplace(heap, entry)

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

        total_records += chunk_n
        total_player_frames += chunk_n * num_players
        offset += chunk_n

    if owns_runtime and runtime is not None:
        runtime.close()

    total_state_flags = total_player_frames * 5
    total_item_slots = total_records * max_items
    native_summary = binding.one_step_summary_finish(summary_handle)
    summary = _emit_summary_from_native(
        native_summary=native_summary,
        reporter=reporter,
        validation_profile=validation_profile,
        print_profile=print_profile,
        num_records=num_records,
        total_records=total_records,
        total_player_frames=total_player_frames,
        total_state_flags=total_state_flags,
        total_item_slots=total_item_slots,
        num_players=num_players,
    )

    if debug_float_fields:
        reporter.print()
        for field in debug_float_fields:
            heap = debug_float_heaps.get(field, [])
            if not heap:
                continue
            reporter.print(f"debug.float.{field}: top {len(heap)} abs errors")
            for ae, gi, seed_frame, ref_frame, pp, out_v, ref_v, seed_a, out_a, ref_a, out_af, ref_af in sorted(
                heap, reverse=True
            ):
                reporter.print(
                    f"  abs_err={ae:.6f} dataset={dataset_path.name} record={gi} seed_frame={seed_frame} ref_frame={ref_frame} p={pp} "
                    f"out={out_v:.6f} ref={ref_v:.6f} "
                    f"seed_action_id={seed_a} out_action_id={out_a} ref_action_id={ref_a} "
                    f"out_action_frame={out_af} ref_action_frame={ref_af}"
                )
    return summary


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dataset", required=True)
    ap.add_argument("--chunk", type=int, default=4096)
    ap.add_argument("--profile", default="rl1_gameplay", help="Validation scoring profile: strict or rl1_gameplay.")
    ap.add_argument("--out", type=Path, default=None, help="optional output path to write the report")
    args = ap.parse_args()

    reporter = Reporter(args.out)
    try:
        evaluate_dataset(dataset_path=Path(args.dataset), chunk=args.chunk, profile=args.profile, reporter=reporter)
    finally:
        reporter.close()


if __name__ == "__main__":
    main()
