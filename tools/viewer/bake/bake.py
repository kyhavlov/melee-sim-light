"""Bake a fighter's animations into a slippilab-style viewer zip.

    python -m bake.bake --character gamewatch --out build/bake/mrGameAndWatch.zip

Output: one <Animation>.json per subaction name, an array of 201 SVG path
strings (frames 0..200) with exact repeats replaced by "frameN" references,
zipped like tools/viewer/assets/character_zips.tsv entries.
"""
from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / 'build'))
sys.path.insert(0, str(HERE.parent))

from bake.anim import Pose, load_figatree, read_subactions  # noqa: E402
from bake.dat import load_dat_file  # noqa: E402
from bake.model import JOBJ_HIDDEN, load_model, skinned_triangles, world_matrices  # noqa: E402
from bake.raster import project, rasterize  # noqa: E402
from bake.trace import trace  # noqa: E402
from bake.visibility import read_vis_tables, simulate_script  # noqa: E402
from derive_gameplay_parts_mask import CommonData, SUBACTION_COUNTS  # noqa: E402

FRAMES = 201
FTPART_TRANSN = 1  # canonical FtPart_TransN
SKIP_PREFIXES = ('TGamewatch', 'TMewtwo', 'TKirby', 'TFox', 'TFalco')

CHARACTERS = {
    # name: (kind, file prefix, ftData root, model root, init selections, overrides)
    'gamewatch': dict(
        kind=24, prefix='Gw', root='ftDataGamewatch',
        model_root='PlyGamewatch5K_Share_joint',
        outline_slot=10,  # ftGw_Init_OnLoad: fp->x5AC.xC[4] = items[10]
        # ftGw_Init: models 0/2/3 shown, 1 and 4..10 hidden.
        defaults=[0, -1, 0, 0, -1, -1, -1, -1, -1, -1, -1],
        # Code-driven states not in the scripts: Oil Panic's bucket
        # (ftGw_SpecialLw_UpdateBucketModel sets model 5 to state 2 on entry).
        overrides={
            'SpecialLw': {5: 2}, 'SpecialLwCatch': {5: 2}, 'SpecialLwShoot': {5: 2},
            'SpecialAirLw': {5: 2}, 'SpecialAirLwCatch': {5: 2}, 'SpecialAirLwShoot': {5: 2},
        },
    ),
}


def animation_name(subaction_name: str) -> str:
    m = subaction_name.split('_ACTION_', 1)
    tail = m[1] if len(m) == 2 else subaction_name
    return tail[:-len('_figatree')] if tail.endswith('_figatree') else tail


def bake_character(name: str, data_dir: Path, out_zip: Path, only: set[str] | None = None,
                   dump_dir: Path | None = None) -> None:
    spec = CHARACTERS[name]
    ftdata = load_dat_file(data_dir / f'Pl{spec["prefix"]}.dat')
    model_dat = load_dat_file(data_dir / f'Pl{spec["prefix"]}Nr.dat')
    aj = (data_dir / f'Pl{spec["prefix"]}AJ.dat').read_bytes()
    model = load_model(model_dat, model_dat.roots[spec['model_root']])
    vis = read_vis_tables(ftdata, spec['root'], extra_item_slot=spec.get('outline_slot'))
    common = CommonData(str(data_dir / 'PlCo.dat'))
    _, p2j, _ = common.parts_table(spec['kind'])
    transn = p2j[FTPART_TRANSN]
    subactions = read_subactions(ftdata, spec['root'], SUBACTION_COUNTS[spec['kind']])

    base_rot = [j.rotation for j in model.joints]
    base_scl = [j.scale for j in model.joints]
    base_tr = [j.translate for j in model.joints]
    parent = [j.parent for j in model.joints]

    def subtree(root: int) -> set[int]:
        out = {root}
        changed = True
        while changed:
            changed = False
            for j in model.joints:
                if j.parent in out and j.index not in out:
                    out.add(j.index)
                    changed = True
        return out

    done: dict[str, list[str]] = {}
    for sub in subactions:
        anim = animation_name(sub.name)
        if anim in done or (only and anim not in only):
            continue
        # Match slippilab's set: item-carry, heavy-item and victim-side throw
        # animations are never selected by the viewer maps.
        if anim.startswith(('Heavy', 'Item', 'T' + spec['prefix'])) or anim.startswith(SKIP_PREFIXES):
            continue
        if sub.anim_size == 0:
            continue
        tree = load_figatree(aj[sub.anim_offset:sub.anim_offset + sub.anim_size], anim)
        pose = Pose(tree, base_rot, base_scl, base_tr)
        override = spec['overrides'].get(anim, {})

        def apply_override(frame, selection, override=override):
            for k, v in override.items():
                if k < len(selection):
                    selection[k] = v
            return selection

        timeline = simulate_script(ftdata, sub.script, spec['defaults'], FRAMES, apply_override)
        frames: list[str] = []
        seen: dict[str, int] = {}
        for f in range(FRAMES):
            rot, scl, tr, hidden_nodes, hidden_branches = pose.evaluate(float(f))
            # slippilab locked TransN's translation so the fighter stays
            # centred; replay position supplies movement.
            tr[transn] = [0.0, 0.0, 0.0]
            world = world_matrices(model, rot, scl, tr)
            hidden_joints = set()
            for j in model.joints:
                if j.flags & JOBJ_HIDDEN:
                    hidden_joints |= subtree(j.index)
            for j in hidden_branches:
                hidden_joints |= subtree(j)
            hidden_joints |= hidden_nodes
            visible = vis.visible(len(model.dobjs), timeline.selections[f])
            visible = {d for d in visible if model.dobjs[d].joint not in hidden_joints}
            tris = skinned_triangles(model, world, visible)
            image = rasterize(project(tris, 2, 1.0))
            if dump_dir is not None:
                dump_dir.mkdir(parents=True, exist_ok=True)
                (dump_dir / f'{anim}_{f}.pgm').write_bytes(
                    b'P5\n1000 1000\n255\n' + ((1 - image) * 255).astype(np.uint8).tobytes())
            path = trace(image)
            ref = seen.get(path)
            if ref is not None:
                frames.append(f'frame{ref}')
            else:
                seen[path] = f
                frames.append(path)
        done[anim] = frames
        print(f'{anim}: {tree.frames:.0f} source frames', file=sys.stderr)

    out_zip.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out_zip, 'w', zipfile.ZIP_DEFLATED) as zf:
        for anim, frames in done.items():
            zf.writestr(f'{anim}.json', json.dumps(frames, separators=(',', ':')))
    print(f'wrote {out_zip} ({len(done)} animations)', file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--character', required=True, choices=sorted(CHARACTERS))
    ap.add_argument('--data', default=str(HERE.parents[2] / 'data' / 'raw'))
    ap.add_argument('--out', required=True)
    ap.add_argument('--only', default='', help='comma-separated animation names')
    ap.add_argument('--dump', default='', help='directory for per-frame PGM dumps')
    args = ap.parse_args()
    only = {s for s in args.only.split(',') if s} or None
    bake_character(args.character, Path(args.data), Path(args.out), only,
                   Path(args.dump) if args.dump else None)
    return 0


if __name__ == '__main__':
    sys.exit(main())
