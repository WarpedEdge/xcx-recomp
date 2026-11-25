#!/usr/bin/env python3
"""Builds the loose RomFS mod that gives every male creator face a mustache and a beard.

The creator's male faces 0 to 2 have no (or only half) facial hair in their head models, so the Mustache and
Beard tabs of the Hair page can only work on faces 3 to 6. This tool copies the facial-hair draws of face 3
(`fc121003`) into the head models of faces 0, 1 and 2 (`fc111002`, `fc121001`, `fc121002`) and writes the
three patched wimdo/wismt pairs as a mod folder:

    <out>/romfs/chr/fc/fc111002.wimdo, fc111002.wismt, ...

Everything is derived from the user's own game files, which are read from an extracted RomFS folder (the one with
`index.json` and `romfs.bin`, made by tools/extract.py). Nothing from the game is kept in the repository.
Run with the `.venv-py314` interpreter (the archive uses zstd, `compression.zstd`).

    python tools/beard_mod.py --romfs build/extracted/upd/romfs --out build/inst/instance/mods/beards

Formats: the model container is the Xenoblade X Definitive Edition ("legacy", version 10040) MXMD layout as written
down by the MIT-licensed xc3_lib project (xc3_lib/src/mxmd/legacy.rs and msrd.rs); only the layout is used, no code.
docs/character-creator-beards.md explains what is changed and why.
"""
import argparse
import json
import os
import struct
import sys
import zlib

M64 = (1 << 64) - 1
P1, P2, P3, P4, P5 = 11400714785074694791, 14029467366897019727, 1609587929392839161, 9650029242287828579, 2870177450012600261


# ---- game archive (sts.arh/sts.ard inside romfs.bin) ----------------------------------------------------------------------------------------------

def _rotl(x, r):
    return ((x << r) | (x >> (64 - r))) & M64


def xxh64(d, seed=0):
    def rnd(a, i):
        return (_rotl((a + i * P2) & M64, 31) * P1) & M64
    n, p = len(d), 0
    if n >= 32:
        v = [(seed + P1 + P2) & M64, (seed + P2) & M64, seed, (seed - P1) & M64]
        while p + 32 <= n:
            for k in range(4):
                v[k] = rnd(v[k], int.from_bytes(d[p:p + 8], 'little'))
                p += 8
        h = (_rotl(v[0], 1) + _rotl(v[1], 7) + _rotl(v[2], 12) + _rotl(v[3], 18)) & M64
        for k in range(4):
            h = ((h ^ rnd(0, v[k])) * P1 + P4) & M64
    else:
        h = (seed + P5) & M64
    h = (h + n) & M64
    while p + 8 <= n:
        h ^= rnd(0, int.from_bytes(d[p:p + 8], 'little'))
        h = (_rotl(h, 27) * P1 + P4) & M64
        p += 8
    if p + 4 <= n:
        h ^= (int.from_bytes(d[p:p + 4], 'little') * P1) & M64
        h = (_rotl(h, 23) * P2 + P3) & M64
        p += 4
    while p < n:
        h ^= (d[p] * P5) & M64
        h = (_rotl(h, 11) * P1) & M64
        p += 1
    h ^= h >> 33
    h = (h * P2) & M64
    h ^= h >> 29
    h = (h * P3) & M64
    h ^= h >> 32
    return h


class Archive:
    def __init__(self, romfs):
        from compression import zstd
        self.zstd = zstd
        index = json.load(open(os.path.join(romfs, 'index.json')))
        self.files = {f['path']: f for f in index['files']}
        self.rom = open(os.path.join(romfs, 'romfs.bin'), 'rb')
        arh = self.read('sts.arh')
        count = struct.unpack_from('<I', arh, 4)[0]
        pos, self.table = 0, {}
        for i in range(count):
            h, size, unpacked = struct.unpack_from('<QII', arh, 16 + 16 * i)
            self.table[h] = (pos, size, unpacked)
            pos = (pos + size + 15) & ~15

    def read(self, path):
        e = self.files[path]
        self.rom.seek(e['offset'])
        return self.rom.read(e['size'])

    def get(self, path):
        entry = self.table.get(xxh64(path.encode()))
        if not entry:
            raise KeyError(path)
        off, size, unpacked = entry
        e = self.files['sts.ard']
        self.rom.seek(e['offset'] + off)
        data = self.rom.read(size)
        return self.zstd.decompress(data[0x30:]) if unpacked else data


# ---- head model files -----------------------------------------------------------------------------------------------------------------------

def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def put32(b, o, v):
    struct.pack_into('<I', b, o, v)


def cstr(b, o):
    return b[o:b.index(b'\0', o)].decode('latin1')


def align(x, a):
    return (x + a - 1) // a * a


# the weight buffer (stream header word `weight_buffer_indices`) a mesh uses, by its flags2
WEIGHT_SLOT = {0x4001: 0, 0x2: 1, 0x8: 3}


class Head:
    """One head model: the wimdo, and the vertex stream (the only xbc1/zlib block) of the wismt."""

    def __init__(self, name, wimdo, wismt):
        self.name, self.w, self.s = name, wimdo, wismt
        assert wimdo[:4] == b'DMXM' and u32(wimdo, 4) == 10040, 'not a version 10040 model'
        self.models, self.materials, self.streaming = u32(wimdo, 8), u32(wimdo, 12), u32(wimdo, 36)
        pos = wismt.find(b'xbc1')
        _, _, csize, self.xbc_hash = struct.unpack_from('<4I', wismt, pos + 4)
        self.xbc_pos = pos
        self.xbc_name = wismt[pos + 20:pos + 48]
        self.vd = zlib.decompress(wismt[pos + 48:pos + 48 + csize])
        m = self.models
        mo, mc = u32(wimdo, m + 24), u32(wimdo, m + 28)
        assert mc == 1
        self.model_at = m + mo
        moff, mcnt = u32(wimdo, self.model_at), u32(wimdo, self.model_at + 4)
        self.meshes = [list(struct.unpack_from('<16I', wimdo, m + moff + 64 * i)) for i in range(mcnt)]
        eo, ec = u32(wimdo, m + 112), u32(wimdo, m + 116)
        self.ext = [(cstr(wimdo, m + u32(wimdo, m + eo + 16 * i)), cstr(wimdo, m + u32(wimdo, m + eo + 16 * i + 4)),
                     u32(wimdo, m + eo + 16 * i + 8), u32(wimdo, m + eo + 16 * i + 12)) for i in range(ec)]
        no, nc = u32(wimdo, m + 100), u32(wimdo, m + 104)
        self.bone_names = [cstr(wimdo, m + no + u32(wimdo, m + no + 4 * i)) for i in range(nc)]
        mo_, mc_ = u32(wimdo, self.materials), u32(wimdo, self.materials + 4)
        self.material_names = [cstr(wimdo, self.materials + u32(wimdo, self.materials + mo_ + 0x88 * i)) for i in range(mc_)]
        vo, vc, io, ic = (u32(self.vd, o) for o in (0, 4, 8, 12))
        self.vbs, self.ibs = [], []
        for i in range(vc):
            off, count, stride, aoff, acount, _ = struct.unpack_from('<6I', self.vd, vo + 24 * i)
            attrs = [struct.unpack_from('<HH', self.vd, aoff + 4 * k) for k in range(acount)]
            self.vbs.append(dict(off=off, count=count, stride=stride, aoff=aoff, attrs=attrs))
        for i in range(ic):
            off, count, a, b = struct.unpack_from('<IIHH', self.vd, io + 12 * i)
            self.ibs.append(dict(off=off, count=count, a=a, b=b))
        sb = self.streaming
        self.entries = [[u32(wimdo, sb + u32(wimdo, sb + 12) + 20 * i + 4 * k) for k in range(5)] for i in range(u32(wimdo, sb + 8))]

    def facial_hair(self):
        """(mesh index, component name, attach bone) of every facial-hair draw (flags1 bit 0x100)."""
        out = []
        for i, me in enumerate(self.meshes):
            if me[0] & 0x100:
                ext = self.ext[(me[8] >> 16) & 0xff]
                out.append((i, ext[0], ext[1]))
        if not out:
            # NPC heads (fc18xxxx) have their facial hair in a plain, always drawn mesh with a "hige" material: it is a
            # beard (mustache and all), so it counts as the lower part
            out = [(i, 'higeDA', 'FC_jaw') for i, me in enumerate(self.meshes) if 'hige' in self.material_names[me[5]].lower()]
        return out


def component_of(head, mesh_index):
    """(name, bone, 1, 0) of the extended component a facial-hair mesh belongs to."""
    me = head.meshes[mesh_index]
    return head.ext[(me[8] >> 16) & 0xff] if me[0] & 0x100 else ('higeDA', 'FC_jaw', 1, 0)


def skin_surface(head):
    """The face skin mesh (flags2 0x4001, the one with the most vertices) as a list of triangles ((x, y, z) x 3)."""
    best = max((m for m in head.meshes if m[1] == 0x4001), key=lambda m: head.vbs[m[2]]['count'])
    vb, ib = head.vbs[best[2]], head.ibs[best[3]]
    pts = [struct.unpack_from('<3f', head.vd, vb['off'] + k * vb['stride']) for k in range(vb['count'])]
    idx = struct.unpack_from('<%dH' % ib['count'], head.vd, ib['off'])
    return [(pts[idx[i]], pts[idx[i + 1]], pts[idx[i + 2]]) for i in range(0, len(idx) - 2, 3)]


def front_depth(tris, x, y):
    """z of the foremost point of the surface above (x, y), or None when no triangle covers it."""
    best = None
    for (ax, ay, az), (bx, by, bz), (cx, cy, cz) in tris:
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-12:
            continue
        u = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / den
        v = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / den
        if u >= -1e-6 and v >= -1e-6 and u + v <= 1 + 1e-6:
            z = u * az + v * bz + (1 - u - v) * cz
            if best is None or z > best:
                best = z
    return best


def brow_uv(head, material):
    """A texture coordinate well inside the eyebrow strip: the centre of the eyebrow triangle nearest to the mean coordinate."""
    mesh = next(m for m in head.meshes if m[5] == material)
    vb, ib = head.vbs[mesh[2]], head.ibs[mesh[3]]
    idx = struct.unpack_from('<%dH' % ib['count'], head.vd, ib['off'])
    uv = {k: struct.unpack_from('<2f', head.vd, vb['off'] + k * vb['stride'] + 16) for k in set(idx)}
    centres = [tuple(sum(uv[idx[i + j]][c] for j in range(3)) / 3 for c in range(2)) for i in range(0, len(idx) - 2, 3)]
    mu, mv = (sum(c[k] for c in centres) / len(centres) for k in range(2))
    return min(centres, key=lambda t: (t[0] - mu) ** 2 + (t[1] - mv) ** 2)


def transplant(recipient, parts, material, extra_z=0.0):
    """Returns (wimdo, wismt) of `recipient` with the facial-hair meshes `parts`, a list of (donor head, mesh index), added.

    The added draws use the recipient's material number `material` (its eyebrow material, which also takes the hair colour).
    """
    R = recipient
    # a donor draw sits on the donor's face surface; the recipient's face is shaped differently (face 0's upper lip lies up to
    # 2 cm behind face 3's), so every added vertex moves along z by the difference of the two surfaces at its (x, y)
    skin_r = skin_surface(R)
    w, vd = bytearray(R.w), bytearray(R.vd)
    donor_cache = {}

    def donor_info(D):
        if id(D) not in donor_cache:
            donor_cache[id(D)] = ({i: R.bone_names.index(n) for i, n in enumerate(D.bone_names) if n in R.bone_names}, skin_surface(D))
        return donor_cache[id(D)]

    wbi = list(struct.unpack_from('<6H', vd, 16))
    vb_desc = [struct.unpack_from('<6I', vd, u32(vd, 0) + 24 * i) for i in range(len(R.vbs))]
    ib_desc = [struct.unpack_from('<IIHH', vd, u32(vd, 8) + 12 * i) for i in range(len(R.ibs))]
    ends = [0x1000]
    for v in R.vbs:
        ends += [v['off'] + v['count'] * v['stride'], v['aoff'] + 4 * len(v['attrs'])]
    ends += [v['off'] + v['count'] * 2 for v in R.ibs]
    pos = align(max(ends), 16)
    writes = []

    def place(data):
        nonlocal pos
        at = pos
        writes.append((at, bytes(data)))
        pos = align(at + len(data), 16)
        return at

    # donor weight entries are appended to the recipient's weight buffer, with the bone bytes mapped by name
    added, remap = {}, {}

    def weight(D, bone_map, slot, old):
        if (id(D), slot, old) in remap:
            return remap[(id(D), slot, old)]
        src = D.vbs[struct.unpack_from('<6H', D.vd, 16)[slot]]
        raw = bytearray(D.vd[src['off'] + 16 * old:src['off'] + 16 * old + 16])
        for k in range(12, 16):
            if raw[k] in bone_map:
                raw[k] = bone_map[raw[k]]
            elif raw[k]:
                raise ValueError('bone %s is not in %s' % (D.bone_names[raw[k]], R.name))
        base = R.vbs[wbi[slot]]['count'] + len(added.setdefault(slot, bytearray())) // 16
        added[slot] += raw
        remap[(id(D), slot, old)] = base
        return base

    exts, new_meshes = list(R.ext), []
    for D, part in parts:
        bone_map, skin_d = donor_info(D)
        me = D.meshes[part]
        vb, ib = D.vbs[me[2]], D.ibs[me[3]]
        idx = struct.unpack_from('<%dH' % ib['count'], D.vd, ib['off'])
        used = sorted(set(idx))
        local = {o: n for n, o in enumerate(used)}
        slot = WEIGHT_SLOT[me[1]]
        # per vertex: the recipient's front depth minus the donor's at the same (x, y); where either face has no skin
        # there (the chin's underside), the part's median shift is used
        shifts = []
        for k in used:
            x, y = struct.unpack_from('<2f', D.vd, vb['off'] + k * vb['stride'])[0:1] + struct.unpack_from('<f', D.vd, vb['off'] + k * vb['stride'] + 4)
            zr, zd = front_depth(skin_r, x, y), front_depth(skin_d, x, y)
            shifts.append(None if zr is None or zd is None else max(-0.03, min(0.03, zr - zd)))
        known = sorted(v for v in shifts if v is not None)
        median = known[len(known) // 2] if known else 0.0
        shift_of = {k: (median if v is None else v) + extra_z for k, v in zip(used, shifts)}
        # the mustache's own texture coordinates (a corner of face 3's beard texture) fall on the transparent part of the
        # eyebrow texture it has to wear here, so it samples the middle of the eyebrow instead; the beard's work as they are
        flat_uv = component_of(D, part)[0].startswith('higeU') or not me[0] & 0x100   # NPC beards: same story
        verts = bytearray()
        for k in used:
            v = bytearray(D.vd[vb['off'] + k * vb['stride']:vb['off'] + (k + 1) * vb['stride']])
            assert vb['attrs'][0] == (0, 12) and vb['attrs'][1] == (3, 4)   # position, then the weight index
            put32(v, 12, weight(D, bone_map, slot, u32(v, 12)))
            struct.pack_into('<f', v, 8, struct.unpack_from('<f', v, 8)[0] + shift_of[k])
            if flat_uv:
                assert vb['attrs'][2] == (5, 8)
                struct.pack_into('<2f', v, 16, *brow_uv(R, material))
            verts += v
        aoff = place(b''.join(struct.pack('<HH', t, s) for t, s in vb['attrs']))
        vb_desc.append((place(verts), len(used), vb['stride'], aoff, len(vb['attrs']), 0))
        ib_desc.append((place(struct.pack('<%dH' % len(idx), *[local[i] for i in idx])), len(idx), 0, 0))
        comp = component_of(D, part)
        if comp[:2] not in [e[:2] for e in exts]:
            exts.append(comp)
        raw = list(me)
        raw[2], raw[3], raw[5] = len(vb_desc) - 1, len(ib_desc) - 1, material
        raw[8] = (raw[8] & ~0xff0000) | ([e[:2] for e in exts].index(comp[:2]) << 16)
        raw[10] = 0
        new_meshes.append(raw)
    for slot, extra in added.items():
        old = R.vbs[wbi[slot]]
        data = bytes(R.vd[old['off']:old['off'] + old['count'] * 16]) + bytes(extra)
        vb_desc.append((place(data), old['count'] + len(extra) // 16, 16, old['aoff'], 2, 0))
        wbi[slot] = len(vb_desc) - 1

    # the vertex region (stream entry 0) grows in 0x1000 steps; the shader and low-texture regions move up with it
    entries = [list(e) for e in R.entries]
    delta = max(0, align(pos, 0x1000) - entries[0][1])
    if delta:
        vd[entries[0][1]:entries[0][1]] = b'\0' * delta
        entries[0][1] += delta
        for e in entries[1:]:
            e[0] += delta
    for at, data in writes:
        vd[at:at + len(data)] = data
    hdr_end = 0x30 + 24 * len(R.vbs) + 12 * len(R.ibs)
    assert not any(vd[hdr_end:0x1000])
    vbo = align(hdr_end, 16)
    ibo = align(vbo + 24 * len(vb_desc), 16)
    assert ibo + 12 * len(ib_desc) <= 0x1000
    for i, t in enumerate(vb_desc):
        struct.pack_into('<6I', vd, vbo + 24 * i, *t)
    for i, t in enumerate(ib_desc):
        struct.pack_into('<IIHH', vd, ibo + 12 * i, *t)
    for o, v in ((0, vbo), (4, len(vb_desc)), (8, ibo), (12, len(ib_desc))):
        put32(vd, o, v)
    struct.pack_into('<6H', vd, 16, *wbi)

    def alloc(data, al=4):
        while len(w) % al:
            w.append(0)
        at = len(w)
        w.extend(data)
        return at

    m = R.models
    if len(exts) != len(R.ext):
        arr = bytearray()
        for name, bone, a, b in exts:
            arr += struct.pack('<IIII', alloc(name.encode() + b'\0', 1) - m, alloc(bone.encode() + b'\0', 1) - m, a, b)
        put32(w, m + 112, alloc(bytes(arr)) - m)
        put32(w, m + 116, len(exts))
    old = bytearray(w[m + u32(w, R.model_at):m + u32(w, R.model_at) + 64 * len(R.meshes)])
    for raw in new_meshes:
        old += struct.pack('<16I', *raw)
    put32(w, R.model_at, alloc(bytes(old)) - m)
    put32(w, R.model_at + 4, len(old) // 64)
    assert u32(w, m + 92) == len(R.ibs)   # the game sizes its per-index-buffer arrays by this word
    put32(w, m + 92, len(ib_desc))

    # both copies of the streaming block (the wimdo's and the wismt header's) carry the stream and entry sizes
    comp = zlib.compress(bytes(vd), 9)
    xbc = b'xbc1' + struct.pack('<4I', 1, len(vd), len(comp), R.xbc_hash) + R.xbc_name + comp
    total = align(len(xbc), 16)

    def patch(buf, base):
        eo, so = u32(buf, base + 12), u32(buf, base + 20)
        for i, e in enumerate(entries):
            struct.pack_into('<2I', buf, base + eo + 20 * i, e[0], e[1])
        put32(buf, base + so, total)
        put32(buf, base + so + 4, len(vd))

    patch(w, R.streaming)
    s = bytearray(R.s[:R.xbc_pos]) + xbc + b'\0' * (total - len(xbc))
    patch(s, 0x10)
    return bytes(w), bytes(s)


# faces 0 to 2 and the facial hair they are missing: (head, 'U' mustache and/or 'D' beard to add). Face 3 (fc121003) is the
# default donor; any creator face with facial hair (fc121001 .. fc131003) or the NPC beards fc181030 / fc181060 can be chosen.
JOBS = (('fc111002', 'UD'), ('fc121001', 'U'), ('fc121002', 'U'))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--romfs', required=True, help='extracted RomFS folder (index.json, romfs.bin)')
    ap.add_argument('--out', required=True, help='mod folder to write (gets romfs/chr/fc/...)')
    ap.add_argument('--mustache-donor', default='fc121003', help='head whose mustache is copied (fc121003, fc131001, fc131002, fc131003)')
    ap.add_argument('--beard-donor', default='fc121003', help='head whose beard is copied (a creator face, or the NPC beards fc181030, fc181060)')
    ap.add_argument('--extra-z', type=float, default=0.0, help='debug: push every added vertex forward by this many metres')
    args = ap.parse_args()
    arc = Archive(args.romfs)

    def load(name):
        return Head(name, arc.get('/chr/fc/%s.wimdo' % name), arc.get('/chr/fc/%s.wismt' % name))

    donors = {'U': load(args.mustache_donor), 'D': load(args.beard_donor)}
    out = os.path.join(args.out, 'romfs', 'chr', 'fc')
    os.makedirs(out, exist_ok=True)
    for name, wanted in JOBS:
        head = load(name)
        material = next(i for i, n in enumerate(head.material_names) if n.startswith('mayu'))
        parts = []
        for kind in wanted:
            found = [(donors[kind], i) for i, comp, _ in donors[kind].facial_hair() if comp[4] == kind]
            if not found:
                raise SystemExit('%s has no %s part' % (donors[kind].name, 'mustache' if kind == 'U' else 'beard'))
            parts += found
        wimdo, wismt = transplant(head, parts, material, args.extra_z)
        open(os.path.join(out, name + '.wimdo'), 'wb').write(wimdo)
        open(os.path.join(out, name + '.wismt'), 'wb').write(wismt)
        print('%s: %s added (mustache from %s, beard from %s), %d + %d bytes' % (name, wanted, donors['U'].name, donors['D'].name, len(wimdo), len(wismt)))


if __name__ == '__main__':
    sys.exit(main())
