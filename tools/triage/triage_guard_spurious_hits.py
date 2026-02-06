from __future__ import annotations

import argparse
import importlib
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from tools.eval.dataset import COMPARE_DTYPE, INPUT_DTYPE, SEED_DTYPE, read_dataset


CONTACT_CLASSIFIED_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("contact_kind", "u1"),  # 0=BODY, 1=SHIELD
        ("hurtcap_id", "u1"),
        ("_pad0", "u1", (3,)),
        ("attacker_msid", "<u2"),
        ("attacker_action_frame", "<i2"),
        ("hitbox_x", "<f4"),
        ("hitbox_y", "<f4"),
        ("hitbox_z", "<f4"),
        ("hitbox_radius", "<f4"),
        ("hitbox_damage", "<f4"),
        ("hurtcap_ax", "<f4"),
        ("hurtcap_ay", "<f4"),
        ("hurtcap_az", "<f4"),
        ("hurtcap_bx", "<f4"),
        ("hurtcap_by", "<f4"),
        ("hurtcap_bz", "<f4"),
        ("hurtcap_radius", "<f4"),
        ("shield_x", "<f4"),
        ("shield_y", "<f4"),
        ("shield_z", "<f4"),
        ("shield_radius", "<f4"),
    ],
    align=False,
)

CONTACT_BODY_DTYPE = np.dtype(
    [
        ("attacker", "u1"),
        ("defender", "u1"),
        ("hitbox_id", "u1"),
        ("hurtcap_id", "u1"),
        ("attacker_msid", "<u2"),
        ("attacker_action_frame", "<i2"),
        ("hitbox_x", "<f4"),
        ("hitbox_y", "<f4"),
        ("hitbox_z", "<f4"),
        ("hitbox_radius", "<f4"),
        ("hitbox_damage", "<f4"),
        ("hurtcap_ax", "<f4"),
        ("hurtcap_ay", "<f4"),
        ("hurtcap_az", "<f4"),
        ("hurtcap_bx", "<f4"),
        ("hurtcap_by", "<f4"),
        ("hurtcap_bz", "<f4"),
        ("hurtcap_radius", "<f4"),
    ],
    align=False,
)


@dataclass(frozen=True)
class Case:
    dataset: Path
    record: int
    p: int


def _load_binding():
    return importlib.import_module("msl_binding")


def _u8hex5(v: np.ndarray) -> str:
    xs = [int(x) & 0xFF for x in v.reshape(-1).tolist()]
    return " ".join(f"{x:02x}" for x in xs)


def _dump_case(*, case: Case, out_dir: Path, max_contacts: int) -> Path:
    ds = read_dataset(str(case.dataset))
    samples = ds.samples
    num_records = int(samples.shape[0])
    if case.record < 0 or case.record >= num_records:
        raise SystemExit(
            f"record out of range: dataset={case.dataset} record={case.record} num_records={num_records}"
        )

    num_players = int(ds.header["num_players"])
    if case.p < 0 or case.p >= num_players:
        raise SystemExit(f"invalid player index: p={case.p} num_players={num_players}")

    row = samples[case.record : case.record + 1]

    binding = _load_binding()
    sizes = binding.sizes()
    seed_stride = int(sizes["seed"])
    input_stride = int(sizes["input"])
    compare_stride = int(sizes["compare"])

    assert seed_stride == SEED_DTYPE.itemsize
    assert input_stride == INPUT_DTYPE.itemsize
    assert compare_stride == COMPARE_DTYPE.itemsize

    handle = binding.init(batch_size=1, num_players=num_players)
    try:
        seed_bytes = np.empty((1, seed_stride), dtype=np.uint8)
        prev_input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        input_bytes = np.empty((1, input_stride), dtype=np.uint8)
        out_compare_bytes = np.empty((1, compare_stride), dtype=np.uint8)

        seed_bytes[:] = np.frombuffer(row["seed_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, seed_stride)
        prev_input_bytes[:] = np.frombuffer(row["prev_input_t"].tobytes(order="C"), dtype=np.uint8).reshape(
            1, input_stride
        )
        input_bytes[:] = np.frombuffer(row["input_t"].tobytes(order="C"), dtype=np.uint8).reshape(1, input_stride)

        binding.reseed_seed(handle, seed_bytes)
        binding.step_input(handle, prev_input_bytes, input_bytes)
        binding.write_compare(handle, out_compare_bytes)

        seed = row["seed_t"].reshape(-1)[0]
        ref = row["ref_t1"].reshape(-1)[0]
        out = out_compare_bytes.view(COMPARE_DTYPE).reshape(-1)[0]

        bubbles = binding.debug_shield_bubbles_world(handle, 0)
        assert bubbles.shape == (4, 4)

        raw_contacts, count = binding.debug_combat_contacts_classified_filtered(handle, 0, max_contacts)
        assert int(raw_contacts.shape[1]) == int(CONTACT_CLASSIFIED_DTYPE.itemsize)
        contacts = raw_contacts.reshape(-1).view(CONTACT_CLASSIFIED_DTYPE)[: int(count)]

        sel_raw, sel_count = binding.debug_combat_select_body_hits(handle, 0, min(64, max_contacts))
        assert int(sel_raw.shape[1]) == int(CONTACT_BODY_DTYPE.itemsize)
        selected = sel_raw.reshape(-1).view(CONTACT_BODY_DTYPE)[: int(sel_count)]

        out_dir.mkdir(parents=True, exist_ok=True)
        out_path = out_dir / f"guard_spurious_{case.dataset.stem}_rec{case.record}_p{case.p}.txt"
        with out_path.open("w", encoding="utf-8") as f:
            f.write(f"dataset={case.dataset}\n")
            f.write(f"record={case.record}\n")
            f.write(f"p={case.p}\n")
            f.write("\n")

            f.write("seed/ref/out (t -> t+1) core fields\n")
            f.write(
                "  action_id: "
                f"{int(seed['action_id'][case.p])} / {int(ref['action_id'][case.p])} / {int(out['action_id'][case.p])}\n"
            )
            f.write(
                "  hitlag:   "
                f"{int(seed['hitlag'][case.p])} / {int(ref['hitlag'][case.p])} / {int(out['hitlag'][case.p])}\n"
            )
            f.write(
                "  hitstun:  "
                f"{int(seed['hitstun'][case.p])} / {int(ref['hitstun'][case.p])} / {int(out['hitstun'][case.p])}\n"
            )
            f.write(
                "  shield_hp:"
                f" {float(seed['shield_hp'][case.p]):.9g} / {float(ref['shield_hp'][case.p]):.9g} / {float(out['shield_hp'][case.p]):.9g}\n"
            )
            f.write(
                "  state_flags:"
                f" {_u8hex5(seed['state_flags'][case.p])} / {_u8hex5(ref['state_flags'][case.p])} / {_u8hex5(out['state_flags'][case.p])}\n"
            )
            f.write("\n")

            f.write("shield bubbles world (x,y,z,r)\n")
            for pp in range(num_players):
                x, y, z, r = (float(bubbles[pp, 0]), float(bubbles[pp, 1]), float(bubbles[pp, 2]), float(bubbles[pp, 3]))
                f.write(f"  p{pp}: {x:.9g} {y:.9g} {z:.9g} {r:.9g}\n")
            f.write("\n")

            f.write(f"debug_combat_contacts_classified_filtered: count={int(count)}\n")
            for c in contacts:
                kind = "SHIELD" if int(c["contact_kind"]) == 1 else "BODY"
                f.write(
                    "  "
                    f"a={int(c['attacker'])} d={int(c['defender'])} kind={kind} "
                    f"hb={int(c['hitbox_id'])} cap={int(c['hurtcap_id']) if kind=='BODY' else -1} "
                    f"dmg={float(c['hitbox_damage']):.9g} "
                    f"hx={float(c['hitbox_x']):.9g} hy={float(c['hitbox_y']):.9g} hr={float(c['hitbox_radius']):.9g} "
                    f"shx={float(c['shield_x']):.9g} shy={float(c['shield_y']):.9g} shr={float(c['shield_radius']):.9g}\n"
                )
            f.write("\n")

            f.write(f"debug_combat_select_body_hits: count={int(sel_count)}\n")
            for c in selected:
                f.write(
                    "  "
                    f"a={int(c['attacker'])} d={int(c['defender'])} "
                    f"hb={int(c['hitbox_id'])} cap={int(c['hurtcap_id'])} "
                    f"dmg={float(c['hitbox_damage']):.9g} "
                    f"hx={float(c['hitbox_x']):.9g} hy={float(c['hitbox_y']):.9g} hr={float(c['hitbox_radius']):.9g}\n"
                )

    finally:
        binding.destroy(handle)

    return out_path


def _default_cases(root: Path) -> list[Case]:
    base = root / "datasets/fox_falco_fd_ucf084_recent/replays/debug/cardinal_1.0_recent"
    return [
        # Cluster 1: 181/181/75
        Case(base / "AttachedGoodNaturedGuanaco.msl", 4046, 1),
        Case(base / "GracefulAttachedTurtle.msl", 2277, 0),
        Case(base / "GracefulAttachedTurtle.msl", 9483, 0),
        # Cluster 2: 182/182/181
        Case(base / "QuerulousGrandDinosaur.msl", 1444, 1),
        # Cluster 3: 181/181/78
        Case(base / "TreasuredBackKangaroo.msl", 3496, 0),
    ]


def main() -> None:
    ap = argparse.ArgumentParser(description="Triage seed==ref guard-family spurious-hit offenders.")
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=Path("reports/triage"),
        help="Directory to write per-case dumps (default: reports/triage).",
    )
    ap.add_argument("--max-contacts", type=int, default=512, help="Max combat contacts to dump per case.")
    ap.add_argument(
        "--case",
        action="append",
        default=[],
        help=(
            "Repeatable case spec 'dataset.msl:record:p' (e.g. datasets/.../TBK.msl:3496:0). "
            "If omitted, runs the representative guard clusters from the current suite."
        ),
    )
    args = ap.parse_args()

    root = Path(__file__).resolve().parents[2]
    cases: list[Case] = []
    if args.case:
        for s in args.case:
            parts = str(s).split(":")
            if len(parts) != 3:
                raise SystemExit(f"bad --case (want dataset:record:p): {s!r}")
            ds_path = Path(parts[0])
            if not ds_path.is_absolute():
                ds_path = (root / ds_path).resolve()
            cases.append(Case(ds_path, int(parts[1]), int(parts[2])))
    else:
        cases = _default_cases(root)

    written: list[Path] = []
    for c in cases:
        written.append(_dump_case(case=c, out_dir=Path(args.out_dir), max_contacts=int(args.max_contacts)))

    for p in written:
        print(p)


if __name__ == "__main__":
    main()
