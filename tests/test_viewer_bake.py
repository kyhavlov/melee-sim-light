"""Silhouette bake (tools/viewer/bake): model decode and manifest integrity."""
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'viewer'))
sys.path.insert(0, str(ROOT / 'tools' / 'build'))

DATA = ROOT / 'data' / 'raw'


def _manifest_rows():
    rows = {}
    for line in (ROOT / 'tools/viewer/assets/character_zips.tsv').read_text().splitlines():
        if not line or line.startswith('#'):
            continue
        name, sha, url = line.split('\t')
        rows[name] = (sha, url)
    return rows


def test_baked_assets_match_manifest():
    rows = _manifest_rows()
    baked = {n: v for n, v in rows.items() if v[1].startswith('baked:')}
    assert 'mrGameAndWatch.zip' in baked
    for name, (sha, url) in baked.items():
        path = ROOT / url[len('baked:'):]
        assert path.is_file(), path
        assert hashlib.sha256(path.read_bytes()).hexdigest() == sha, name


@pytest.mark.skipif(not (DATA / 'PlGwNr.dat').is_file(), reason='needs extracted data/raw')
def test_gamewatch_model_and_visibility_decode():
    from bake.dat import load_dat_file
    from bake.model import load_model
    from bake.visibility import read_vis_tables

    dat = load_dat_file(DATA / 'PlGwNr.dat')
    model = load_model(dat, dat.roots['PlyGamewatch5K_Share_joint'])
    assert len(model.joints) == 53
    assert len(model.dobjs) == 116
    ftdata = load_dat_file(DATA / 'PlGw.dat')
    vis = read_vis_tables(ftdata, 'ftDataGamewatch', extra_item_slot=10)
    assert vis.model_num == 11
    # Every DObj is either body (unlisted) or in a lookup; the outline copies
    # (group 4) cover what the costume tables leave out.
    assert vis.listed() | {d for d in range(116) if d not in vis.listed()} == set(range(116))
    body = vis.visible(116, [0, -1, 0, 0, -1, -1, -1, -1, -1, -1, -1])
    assert {0, 17, 18, 19, 20, 21, 22, 36, 37, 38, 39, 40, 41} == body


@pytest.mark.skipif(not (DATA / 'PlGwAJ.dat').is_file(), reason='needs extracted data/raw')
def test_gamewatch_animation_matches_bind_translations():
    from bake.anim import FObjState, load_figatree, read_subactions
    from bake.dat import load_dat_file
    from bake.model import load_model
    from derive_gameplay_parts_mask import SUBACTION_COUNTS

    ftdata = load_dat_file(DATA / 'PlGw.dat')
    aj = (DATA / 'PlGwAJ.dat').read_bytes()
    dat = load_dat_file(DATA / 'PlGwNr.dat')
    model = load_model(dat, dat.roots['PlyGamewatch5K_Share_joint'])
    subs = read_subactions(ftdata, 'ftDataGamewatch', SUBACTION_COUNTS[24])
    wait = next(s for s in subs if s.name.endswith('_Wait1_figatree'))
    tree = load_figatree(aj[wait.anim_offset:wait.anim_offset + wait.anim_size], 'Wait1')
    assert len(tree.joint_tracks) == 53
    assert tree.frames == 60.0
    # Constant translation tracks reproduce the skeleton's bind translations.
    for joint in (3, 6, 9, 17, 19, 20, 24, 32, 51):
        tracks = {t.obj_type: t for t in tree.joint_tracks[joint]}
        for axis, kind in enumerate((5, 6, 7)):
            if kind in tracks:
                value = FObjState(tracks[kind]).evaluate(0.0)
                assert abs(value - model.joints[joint].translate[axis]) < 0.02, (joint, axis)
