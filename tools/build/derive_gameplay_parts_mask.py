#!/usr/bin/env python3
"""Derive a gameplay part-admission row for src/melee/ft/ftparts.c.

The admitted set is the union of:
  - the canonical gameplay skeleton (FtPart ids reached by supported common
    code), transferred through PlCo.dat's per-character part_to_joint table;
  - the character's own data-driven bones from its Pl<Xx>.dat ftData:
    hurtbox owners (x30), dynamics chains (x2C), model-desc anchors
    (x8->x10..x14: anim root, and throw/item attach bones), the x34 shield
    bone, and the x54 effect-anchor cycle;
  - every Create Hitbox bone reachable from the fighter's subaction
    scripts (a cold hitbox part has no world matrix, so its capsule
    whiffs at the origin);
  - every skeleton ancestor of the above (the compact loader requires an
    ancestor-closed keep set; see msl_core_HSD_JObjLoadJointFiltered).

The canonical set is anchored to Luigi's audited row (commit 1033eb71), which
this formula reproduces bit-exactly; run with `luigi` to re-verify before
trusting output for a new character.

Usage: python3 tools/build/derive_gameplay_parts_mask.py luigi mario drmario
"""
import struct
import sys

# internal FighterKind, Pl prefix, ftData root symbol
CHARACTERS = {
    'mario': (0, 'Mr', 'ftDataMario'),
    'fox': (1, 'Fx', 'ftDataFox'),
    'captain': (2, 'Ca', 'ftDataCaptain'),
    'seak': (7, 'Sk', 'ftDataSeak'),
    'donkey': (3, 'Dk', 'ftDataDonkey'),
    'ganon': (25, 'Gn', 'ftDataGanon'),
    'koopa': (5, 'Kp', 'ftDataKoopa'),
    'peach': (9, 'Pe', 'ftDataPeach'),
    'popo': (10, 'Pp', 'ftDataPopo'),
    'nana': (11, 'Nn', 'ftDataNana'),
    'pikachu': (12, 'Pk', 'ftDataPikachu'),
    'samus': (13, 'Ss', 'ftDataSamus'),
    'yoshi': (14, 'Ys', 'ftDataYoshi'),
    'mewtwo': (16, 'Mt', 'ftDataMewtwo'),
    'gamewatch': (24, 'Gw', 'ftDataGamewatch'),
    'ness': (8, 'Ns', 'ftDataNess'),
    'link': (6, 'Lk', 'ftDataLink'),
    'clink': (20, 'Cl', 'ftDataClink'),
    'purin': (15, 'Pr', 'ftDataPurin'),
    'luigi': (17, 'Lg', 'ftDataLuigi'),
    'mars': (18, 'Ms', 'ftDataMars'),
    'emblem': (26, 'Fe', 'ftDataEmblem'),
    'zelda': (19, 'Zd', 'ftDataZelda'),
    'drmario': (21, 'Dr', 'ftDataDrmario'),
    'falco': (22, 'Fc', 'ftDataFalco'),
}

# Subaction counts per FighterKind (src/melee/ft/ftdata.c ftData_Table_Unk0).
SUBACTION_COUNTS = {
    0: 303, 1: 327, 2: 318, 3: 337, 6: 314, 7: 317, 8: 326, 9: 318, 10: 321,
    11: 321,
    12: 320, 13: 313, 14: 314, 15: 327, 16: 314, 17: 312, 18: 327, 19: 311, 20: 314,
    21: 303,
    22: 327,
    24: 323,
    26: 327,
    25: 318,
    5: 316,
}

# Words consumed per fighter subaction event with opcode >= 10, indexed by
# opcode - 10 (src/melee/ft/ftaction.c ftAction_803C0870; retail's own
# fast-skip executor ftAction_8007349C advances by exactly these strides).
FT_EVENT_WORDS = [
    5, 5, 1, 1, 1, 1, 1, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1, 7, 4, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 3, 2, 1, 4,
]
CREATE_HITBOX_OPCODE = 11

FTPART_INVALID = 0xFF
NCANON = 54  # FtPart_TopN .. FtPart_TransN2

# Raw fp->parts[...] indices reached directly by character code (article
# spawn anchors the DAT graphs cannot expose). These join the mask before
# ancestor closure.
#   samus: ftSs_SpecialN.c charge-shot spawn parts[FtPart_RHandNb=50],
#          ftSs_Init.c/ftCo_AirCatch.c/ftCo_Attack100.c throw and grapple
#          anchor parts[FtPart_ThrowN=51], ftSs_SpecialN.c missile spawn
#          parts[FtPart_56=56].
CODE_ANCHORED = {
    # Koopa: ftKp_SpecialN spawns Flame at raw mouth part 48.
    'koopa': (48,),
    'samus': (50, 51, 56),
    #   popo/nana: ftPp_SpecialN.c ice-shot spawn parts[FtPart_TopN=0],
    #          ftPp_SpecialLw.c blizzard anchors parts[FtPart_L3rdNa=26] and
    #          parts[FtPart_L4thNb=29], ftPp_SpecialHi.c/ftNn_SpecialHi.c/
    #          itclimbersstring.c belay tether reads BOTH climbers'
    #          parts[FtPart_L4thNb=29], popo's parts[FtPart_R4thNb=47], and
    #          nana's parts[FtPart_XRotN=2]; each climber runs the shared
    #          ftPp_* handlers, so the anchor set is symmetric.
    'popo': (0, 2, 26, 29, 47),
    'nana': (0, 2, 26, 29, 47),
    #   ness: ftNs_AttackHi4.c transforms the Yo-Yo hitbox position through
    #          the raw parts[61] joint on every charge/release frame.
    'ness': (61,),
    # ftMt_SpecialN/Lw read these raw shoulder/hand/Disable anchors.
    'mewtwo': (27, 32, 35),
    # ftGw Attack11/Lw3/S4, AttackAir and SpecialHi/Lw article anchors.
    'gamewatch': (0, 1, 17, 21, 32),
    #   link/clink: ftLk_SpecialHi.c anchors the spin-attack effect on the
    #          raw parts[FtPart_L2ndNa=24] joint for Link and
    #          parts[FtPart_L3rdNa=26] for Young Link; ftParts_800753D4
    #          attaches the sword accessory under parts[67] (Link) and
    #          parts[71] (Young Link) at OnLoad and binds the loaded JObj at
    #          the accessory part itself -- 68 (Link) and 72 (Young Link),
    #          the Fighter_804D6540 entry's x0 -- which must stay live so
    #          the retail ftAnim_8006E7B8 tree/part pairing (which counts
    #          the attached accessory node) stays aligned in the compact
    #          hosted tree. itlinkhookshot.c's parts[139] chain-joint store
    #          is a runtime-anchored article slot past the skeleton, like
    #          Samus's zair beam tip, and stays outside the admission mask.
    #          Both Links also spawn/attach every article through
    #          ftParts_GetBoneIndex(FtPart_LThumbNb=31 / FtPart_RThumbNb=49):
    #          the left thumb resolves to parts[35] (Link) / parts[37]
    #          (Young Link) and anchors the bomb pull, boomerang throw, and
    #          milk bottle; the right thumb resolves to parts[64]/[68],
    #          already graph-admitted, and anchors the arrow/bow/hookshot.
    'link': (24, 35, 67, 68),
    'clink': (26, 37, 71, 72),
    #   pikachu: ftPk_SpecialLw.c thunder-loop efAsync anchors
    #          parts[FtPart_TopN=0] raw; every other part access resolves
    #          through ftParts_GetBoneIndex.
    'pikachu': (0,),
    #   donkey: ftDk_SpecialHi.c/ftDk_SpecialN.c spawn efSync on
    #          parts[FtPart_TopN=0] raw and ftDk_SpecialLw.c on
    #          parts[FtPart_TransN=1].
    'donkey': (0, 1),
    # Yoshi: Egg Roll transforms root/rotation parts and samples part 4;
    # SpecialHi samples part 31 and SpecialLw samples TransN (1).
    'yoshi': (0, 1, 2, 4, 31),
}

# Luigi's audited admission row (joint indices), the canonical anchor.
LUIGI_AUDITED = [
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 22, 23, 24, 29, 30, 31,
    32, 33, 42, 43, 44, 47, 48, 49, 50, 51, 53, 54, 55, 56, 57, 59,
]


def load_dat(path):
    d = open(path, 'rb').read()
    _, dsize, nrel, nroot, nxref = struct.unpack('>IIIII', d[:20])
    data = d[0x20:0x20 + dsize]
    reloc_off = 0x20 + dsize
    relocs = set(struct.unpack('>%dI' % nrel, d[reloc_off:reloc_off + nrel * 4]))
    roots_off = reloc_off + nrel * 4
    strtab = roots_off + (nroot + nxref) * 8
    roots = {}
    for i in range(nroot + nxref):
        doff, soff = struct.unpack('>II', d[roots_off + i * 8:roots_off + i * 8 + 8])
        end = d.index(b'\0', strtab + soff)
        roots[d[strtab + soff:end].decode()] = doff
    return data, roots, relocs


def u32(data, off):
    return struct.unpack('>I', data[off:off + 4])[0]


class CommonData:
    def __init__(self, path='data/raw/PlCo.dat'):
        self.data, roots, _ = load_dat(path)
        pdata = roots['ftLoadCommonData']
        self.parts_table_arr = u32(self.data, pdata + 4 * 4)
        self.excl_arr = u32(self.data, pdata + 5 * 4)

    def parts_table(self, kind):
        t = u32(self.data, self.parts_table_arr + kind * 4)
        j2p_off = u32(self.data, t)
        p2j_off = u32(self.data, t + 4)
        n = u32(self.data, t + 8)
        j2p = list(self.data[j2p_off:j2p_off + n])
        p2j = list(self.data[p2j_off:p2j_off + NCANON])
        return j2p, p2j, n

    def exclusions(self, kind):
        # Fighter_804D6540 (ftLoadCommonData pData[5]): accessory-model
        # descriptors of four u8s {part, attach_part, mode, tree_depth};
        # ftParts_8007506C matches entry->x0 against a part id. The listed
        # accessory parts live outside the costume skeleton preorder.
        # refs/melee/src/melee/ft/{fighter.h,ftparts.c::ftParts_8007506C}
        t = u32(self.data, self.excl_arr + kind * 4)
        if t == 0:
            return set()
        arr = u32(self.data, t)
        cnt = u32(self.data, t + 4)
        return {self.data[arr + i * 4] for i in range(cnt)}


def skeleton(path):
    """Preorder joint list of the costume skeleton with parent indices."""
    data, roots, _ = load_dat(path)
    cands = [o for n, o in roots.items()
             if n.endswith('_joint') and 'matanim' not in n]
    assert len(cands) == 1, sorted(roots)
    order, parents = [], []
    JOBJ_INSTANCE = 0x1000

    def walk(off, parent):
        while off:
            idx = len(order)
            order.append(off)
            parents.append(parent)
            flags = u32(data, off + 4)
            child = u32(data, off + 8)
            nxt = u32(data, off + 12)
            if child and not (flags & JOBJ_INSTANCE):
                walk(child, idx)
            off = nxt

    walk(cands[0], -1)
    return order, parents


def ftdata_parts(path, root_name):
    """Data-driven gameplay bones from a character's ftData."""
    data, roots, relocs = load_dat(path)
    ft = roots[root_name]

    def ptr(off):
        # a pointer field is valid iff its offset is relocated (0 is a legal
        # data-section offset: Mario/Luigi place the x8 model desc there)
        return u32(data, off) if off in relocs else None

    bones = set()
    dyn = []
    x30 = ptr(ft + 0x30)  # hurtboxes
    if x30 is not None:
        cnt = u32(data, x30)
        inits = ptr(x30 + 4)
        for i in range(cnt):
            bones.add(u32(data, inits + i * 0x28))
    x2C = ptr(ft + 0x2C)  # dynamics
    if x2C is not None:
        dnum = u32(data, x2C)
        barr = ptr(x2C + 4)
        for i in range(dnum):
            dyn.append((u32(data, barr + i * 0x18),
                        u32(data, barr + i * 0x18 + 8)))
    x8 = ptr(ft + 8)  # model desc anchors
    if x8 is not None:
        for boff in (0x10, 0x11, 0x12, 0x13, 0x14):
            bones.add(data[x8 + boff])
    x34 = ptr(ft + 0x34)  # shield bone
    if x34 is not None:
        bones.add(u32(data, x34))
    x54 = ptr(ft + 0x54)  # effect anchor cycle
    if x54 is not None:
        for i in range(5):
            bones.add(u32(data, x54 + i * 4))
    return bones, dyn


def subaction_hitbox_parts(path, root_name, count, p2j, parts_num):
    """Part indices carrying Create Hitbox capsules in any subaction script.

    Walks every fighter subaction command stream (following Subroutine and
    Goto edges) and collects the bone field of each Create Hitbox event
    (opcode 11). use_common_bone_ids routes the id through part_to_joint,
    matching ftAction_8007121C's fp->parts[...] placement; a hitbox whose
    part is masked cold never gets a world matrix, so its capsule sits at
    the origin and silently whiffs (found via Samus bair's 14% sweetspot
    on part 18).
    """
    data, roots, relocs = load_dat(path)
    ft = roots[root_name]
    if ft + 0xC not in relocs:
        return set()
    table = u32(data, ft + 0xC)
    parts = set()
    seen = set()
    stack = []
    for i in range(count):
        entry = table + i * 0x18
        if entry + 0xC in relocs:
            stack.append(u32(data, entry + 0xC))
    while stack:
        off = stack.pop()
        while off not in seen and off + 4 <= len(data):
            seen.add(off)
            word = u32(data, off)
            op = word >> 26
            if op == 0:  # End
                break
            if op in (5, 7):  # Subroutine / Goto: next word is the target
                if off + 4 in relocs:
                    stack.append(u32(data, off + 4))
                if op == 7:
                    break
                off += 8
                continue
            if op < 10:
                off += 4
                continue
            assert op - 10 < len(FT_EVENT_WORDS), (path, hex(off), op)
            if op == CREATE_HITBOX_OPCODE:
                bone = (word >> 11) & 0xFF
                if (word >> 10) & 1:  # use_common_bone_ids
                    bone = p2j[bone]
                # Raw ids past the skeleton (e.g. Samus zair's beam-tip
                # slot 139) are runtime-anchored article joints, not
                # admission-gated skeleton parts.
                if bone != FTPART_INVALID and bone < parts_num:
                    parts.add(bone)
            off += 4 * FT_EVENT_WORDS[op - 10]
    return parts


def derive(name, common, canon):
    kind, prefix, root = CHARACTERS[name]
    _, p2j, parts_num = common.parts_table(kind)
    excl = common.exclusions(kind)

    mask = set()
    for c in sorted(canon):
        j = p2j[c]
        if j != FTPART_INVALID:
            mask.add(j)

    bones, dyn = ftdata_parts(f'data/raw/Pl{prefix}.dat', root)
    bones |= subaction_hitbox_parts(f'data/raw/Pl{prefix}.dat', root,
                                    SUBACTION_COUNTS[kind], p2j, parts_num)
    order, parents = skeleton(f'data/raw/Pl{prefix}Nr.dat')
    phys = [p for p in range(parts_num) if p not in excl]
    assert len(phys) == len(order), (name, len(phys), len(order))
    part_of_node = {n: p for n, p in enumerate(phys)}
    node_of_part = {p: n for n, p in enumerate(phys)}

    for bone_id, count in dyn:  # chains are linear preorder runs
        n0 = node_of_part[bone_id]
        for n in range(n0, min(n0 + count, len(order))):
            bones.add(part_of_node[n])
    mask |= bones
    mask |= set(CODE_ANCHORED.get(name, ()))

    changed = True
    while changed:
        changed = False
        for p in sorted(mask):
            n = node_of_part.get(p)
            if n is None:
                continue
            pa = parents[n]
            if pa >= 0 and part_of_node[pa] not in mask:
                mask.add(part_of_node[pa])
                changed = True
    return sorted(mask), parts_num


def emit_row(name, mask):
    kind_enum = 'FTKIND_' + ('DRMARIO' if name == 'drmario' else name.upper())
    parts = [f'[{p}] = 1,' for p in mask]
    print(f'    [{kind_enum}] = {{')
    for i in range(0, len(parts), 6):
        print('        ' + ' '.join(parts[i:i + 6]))
    print('    },')


def main():
    names = sys.argv[1:] or ['luigi']
    common = CommonData()
    lj2p, _, _ = common.parts_table(CHARACTERS['luigi'][0])
    canon = {lj2p[j] for j in LUIGI_AUDITED if lj2p[j] != FTPART_INVALID}
    lg, _ = derive('luigi', common, canon)
    assert lg == LUIGI_AUDITED, ('luigi anchor drifted', lg)
    for name in names:
        mask, parts_num = derive(name, common, canon)
        print(f'// {name}: {len(mask)} live / {parts_num - len(mask)} cold '
              f'of {parts_num} parts')
        emit_row(name, mask)


if __name__ == '__main__':
    main()
