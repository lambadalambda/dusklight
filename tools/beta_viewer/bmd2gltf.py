"""Convert a J3D BMD/BDL model to glTF 2.0 (static bind pose, textures as PNG).

Geometry is fully baked: per-vertex draw matrices (DRW1/EVP1 skinning at bind
pose) are applied on the CPU, so the output needs no skeleton. Materials carry
the first TEV stage's texture only — enough for a viewer, not a renderer.
"""
import json
import math
import struct
import sys
import zlib
from pathlib import Path


def u8(d, o):
    return d[o]


def u16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def s16(d, o):
    return struct.unpack_from(">h", d, o)[0]


def u32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def f32(d, o):
    return struct.unpack_from(">f", d, o)[0]


# ---------------------------------------------------------------- chunks


def find_chunks(d):
    assert d[:4] == b"J3D2", "not a J3D model"
    count = u32(d, 0x0C)
    chunks = {}
    off = 0x20
    for _ in range(count):
        tag = d[off : off + 4].decode("ascii")
        size = u32(d, off + 4)
        chunks[tag] = (off, size)
        off += size
    return chunks


# ---------------------------------------------------------------- VTX1

ATTR_PNMTXIDX = 0
ATTR_POS = 9
ATTR_NRM = 10
ATTR_CLR0 = 11
ATTR_TEX0 = 13

COMP_SIZES = {0: 1, 1: 1, 2: 2, 3: 2, 4: 4}


def parse_vtx1(d, off, size):
    fmt_off = off + u32(d, off + 0x08)
    data_offs = [u32(d, off + 0x0C + 4 * i) for i in range(13)]
    # array end = next non-zero data offset (sorted), else chunk end
    nz = sorted(o for o in data_offs if o)
    ends = {}
    for o in nz:
        later = [x for x in nz if x > o]
        ends[o] = later[0] if later else size

    arrays = {}
    fo = fmt_off
    while u32(d, fo) != 0xFF:
        attr = u32(d, fo)
        comp_count = u32(d, fo + 4)
        comp_type = u32(d, fo + 8)
        shift = u8(d, fo + 12)
        fo += 16

        slot = {ATTR_POS: 0, ATTR_NRM: 1, ATTR_CLR0: 3, 12: 4}.get(attr)
        if slot is None and ATTR_TEX0 <= attr <= 20:
            slot = 5 + (attr - ATTR_TEX0)
        if slot is None or data_offs[slot] == 0:
            continue
        start = data_offs[slot]
        end = ends[start]
        raw = d[off + start : off + end]

        if attr in (ATTR_CLR0, 12):
            arrays[attr] = decode_colors(raw, comp_type)
            continue

        ncomp = {ATTR_POS: 3 if comp_count == 1 else 2, ATTR_NRM: 3}.get(attr)
        if ncomp is None:  # texcoords
            ncomp = 2 if comp_count == 1 else 1
        scale = 1.0 / (1 << shift)
        fmt_char = {0: "B", 1: "b", 2: "H", 3: "h", 4: "f"}[comp_type]
        csize = COMP_SIZES[comp_type]
        n = len(raw) // (csize * ncomp)
        vals = struct.unpack(f">{n * ncomp}{fmt_char}", raw[: n * ncomp * csize])
        if comp_type == 4:
            arr = [vals[i * ncomp : (i + 1) * ncomp] for i in range(n)]
        else:
            arr = [
                tuple(v * scale for v in vals[i * ncomp : (i + 1) * ncomp])
                for i in range(n)
            ]
        arrays[attr] = arr
    return arrays


def decode_colors(raw, comp_type):
    out = []
    if comp_type == 5:  # RGBA8
        for i in range(0, len(raw) - 3, 4):
            out.append((raw[i] / 255, raw[i + 1] / 255, raw[i + 2] / 255, raw[i + 3] / 255))
    elif comp_type == 1:  # RGB8
        for i in range(0, len(raw) - 2, 3):
            out.append((raw[i] / 255, raw[i + 1] / 255, raw[i + 2] / 255, 1.0))
    elif comp_type == 3:  # RGBA4
        for i in range(0, len(raw) - 1, 2):
            v = (raw[i] << 8) | raw[i + 1]
            out.append((((v >> 12) & 0xF) / 15, ((v >> 8) & 0xF) / 15, ((v >> 4) & 0xF) / 15, (v & 0xF) / 15))
    else:  # fallback: treat as RGBA8
        for i in range(0, len(raw) - 3, 4):
            out.append((raw[i] / 255, raw[i + 1] / 255, raw[i + 2] / 255, raw[i + 3] / 255))
    return out


# ---------------------------------------------------------------- EVP1 / DRW1


def parse_evp1(d, off, size):
    count = u16(d, off + 8)
    if count == 0:
        return [], {}
    counts_off = off + u32(d, off + 0x0C)
    idx_off = off + u32(d, off + 0x10)
    w_off = off + u32(d, off + 0x14)
    m_off = off + u32(d, off + 0x18)
    envelopes = []
    k = 0
    for i in range(count):
        n = u8(d, counts_off + i)
        env = []
        for j in range(n):
            ji = u16(d, idx_off + 2 * (k + j))
            w = f32(d, w_off + 4 * (k + j))
            env.append((ji, w))
        k += n
        envelopes.append(env)

    inv_binds = {}
    for ji, _ in [(p, w) for env in envelopes for p, w in env]:
        if ji not in inv_binds:
            m = [f32(d, m_off + ji * 0x30 + 4 * c) for c in range(12)]
            inv_binds[ji] = [m[0:4], m[4:8], m[8:12], [0, 0, 0, 1]]
    return envelopes, inv_binds


def parse_drw1(d, off, size):
    count = u16(d, off + 8)
    flags_off = off + u32(d, off + 0x0C)
    idx_off = off + u32(d, off + 0x10)
    return [(u8(d, flags_off + i) != 0, u16(d, idx_off + 2 * i)) for i in range(count)]


# ---------------------------------------------------------------- JNT1


def parse_jnt1(d, off, size):
    count = u16(d, off + 8)
    ent_off = off + u32(d, off + 0x0C)
    joints = []
    for i in range(count):
        e = ent_off + i * 0x40
        sx, sy, sz = (f32(d, e + 4), f32(d, e + 8), f32(d, e + 12))
        rx = s16(d, e + 16) / 0x8000 * math.pi
        ry = s16(d, e + 18) / 0x8000 * math.pi
        rz = s16(d, e + 20) / 0x8000 * math.pi
        tx, ty, tz = (f32(d, e + 24), f32(d, e + 28), f32(d, e + 32))
        joints.append((sx, sy, sz, rx, ry, rz, tx, ty, tz))
    return joints


def mat_mul(a, b):
    return [
        [sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)]
        for i in range(4)
    ]


def joint_local_matrix(j):
    sx, sy, sz, rx, ry, rz, tx, ty, tz = j
    cx, sx_ = math.cos(rx), math.sin(rx)
    cy, sy_ = math.cos(ry), math.sin(ry)
    cz, sz_ = math.cos(rz), math.sin(rz)
    rxm = [[1, 0, 0, 0], [0, cx, -sx_, 0], [0, sx_, cx, 0], [0, 0, 0, 1]]
    rym = [[cy, 0, sy_, 0], [0, 1, 0, 0], [-sy_, 0, cy, 0], [0, 0, 0, 1]]
    rzm = [[cz, -sz_, 0, 0], [sz_, cz, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    sm = [[sx, 0, 0, 0], [0, sy, 0, 0], [0, 0, sz, 0], [0, 0, 0, 1]]
    tm = [[1, 0, 0, tx], [0, 1, 0, ty], [0, 0, 1, tz], [0, 0, 0, 1]]
    return mat_mul(tm, mat_mul(rzm, mat_mul(rym, mat_mul(rxm, sm))))


def transform_point(m, p):
    x, y, z = p[0], p[1], p[2] if len(p) > 2 else 0.0
    return (
        m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3],
        m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3],
        m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3],
    )


def transform_vector(m, p):
    x, y, z = p
    v = (
        m[0][0] * x + m[0][1] * y + m[0][2] * z,
        m[1][0] * x + m[1][1] * y + m[1][2] * z,
        m[2][0] * x + m[2][1] * y + m[2][2] * z,
    )
    l = math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2) or 1.0
    return (v[0] / l, v[1] / l, v[2] / l)


# ---------------------------------------------------------------- INF1


def parse_inf1(d, off, size):
    """Walk the scene graph; return shape index -> material index."""
    hier_off = off + u32(d, off + 0x14)
    shape_mat = {}
    parents = []  # joint stack not needed; track current material
    cur_mat = 0
    o = hier_off
    last = None
    joint_parents = {}
    joint_stack = []
    while True:
        t, idx = u16(d, o), u16(d, o + 2)
        o += 4
        if t == 0x00:
            break
        elif t == 0x01:
            if last is not None and last[0] == 0x10:
                joint_stack.append(last[1])
        elif t == 0x02:
            if joint_stack:
                joint_stack.pop()
        elif t == 0x10:
            joint_parents[idx] = joint_stack[-1] if joint_stack else None
        elif t == 0x11:
            cur_mat = idx
        elif t == 0x12:
            shape_mat[idx] = cur_mat
        last = (t, idx)
    return shape_mat, joint_parents


# ---------------------------------------------------------------- SHP1

PRIM_VERTS = {0x90: "tris", 0x98: "strip", 0xA0: "fan", 0x80: "quads"}


def parse_shp1(d, off, size, arrays, draw_matrices):
    batch_count = u16(d, off + 8)
    batches_off = off + u32(d, off + 0x0C)
    attribs_off = off + u32(d, off + 0x18)
    mtx_table_off = off + u32(d, off + 0x1C)
    data_off = off + u32(d, off + 0x20)
    mtx_data_off = off + u32(d, off + 0x24)
    pkt_loc_off = off + u32(d, off + 0x28)

    shapes = []
    for b in range(batch_count):
        e = batches_off + b * 0x28
        packet_count = u16(d, e + 2)
        attr_rel = u16(d, e + 4)
        first_mtx_data = u16(d, e + 6)
        first_pkt = u16(d, e + 8)

        attribs = []
        ao = attribs_off + attr_rel
        while u32(d, ao) != 0xFF:
            attribs.append((u32(d, ao), u32(d, ao + 4)))
            ao += 8

        verts = []  # (pos, nrm, uv, color)
        tris = []
        mtx_slots = [0xFFFF] * 10
        for p in range(packet_count):
            md = mtx_data_off + (first_mtx_data + p) * 8
            mcount = u16(d, md + 2)
            mfirst = u32(d, md + 4)
            for s in range(mcount):
                v = u16(d, mtx_table_off + 2 * (mfirst + s))
                if v != 0xFFFF:
                    mtx_slots[s] = v

            pl = pkt_loc_off + (first_pkt + p) * 8
            psize = u32(d, pl)
            poff = data_off + u32(d, pl + 4)
            decode_packet(d, poff, psize, attribs, arrays, draw_matrices,
                          mtx_slots, verts, tris)
        shapes.append((verts, tris))
    return shapes


def decode_packet(d, o, size, attribs, arrays, draw_matrices, mtx_slots, verts, tris):
    end = o + size
    has_pn = any(a == ATTR_PNMTXIDX for a, _ in attribs)
    while o < end:
        op = d[o]
        o += 1
        if op == 0:
            continue
        if op not in PRIM_VERTS:
            break
        count = u16(d, o)
        o += 2
        base = len(verts)
        for _ in range(count):
            mtx = draw_matrices[mtx_slots[0]] if mtx_slots[0] != 0xFFFF else None
            pos = nrm = uv = col = None
            for attr, atype in attribs:
                if atype == 1 or attr == ATTR_PNMTXIDX or (1 <= attr <= 8):
                    val = d[o]
                    o += 1
                    if attr == ATTR_PNMTXIDX:
                        slot = val // 3
                        if mtx_slots[slot] != 0xFFFF:
                            mtx = draw_matrices[mtx_slots[slot]]
                    continue
                idx = d[o] if atype == 2 else u16(d, o)
                o += 1 if atype == 2 else 2
                if attr == ATTR_POS:
                    a = arrays.get(ATTR_POS, [])
                    pos = a[idx] if idx < len(a) else (0, 0, 0)
                elif attr == ATTR_NRM:
                    a = arrays.get(ATTR_NRM, [])
                    nrm = a[idx] if idx < len(a) else (0, 1, 0)
                elif attr in (ATTR_CLR0, 12):
                    a = arrays.get(attr, [])
                    if attr == ATTR_CLR0:
                        col = a[idx] if idx < len(a) else (1, 1, 1, 1)
                elif ATTR_TEX0 <= attr <= 20:
                    if attr == ATTR_TEX0:
                        a = arrays.get(ATTR_TEX0, [])
                        uv = a[idx] if idx < len(a) else (0, 0)
            if pos is None:
                pos = (0, 0, 0)
            if mtx is not None:
                pos = transform_point(mtx, pos)
                if nrm is not None:
                    nrm = transform_vector(mtx, nrm)
            verts.append((pos, nrm, uv, col))

        n = count
        kind = PRIM_VERTS[op]
        if kind == "tris":
            for i in range(0, n - 2, 3):
                tris.append((base + i, base + i + 1, base + i + 2))
        elif kind == "strip":
            for i in range(n - 2):
                if i % 2 == 0:
                    tris.append((base + i, base + i + 1, base + i + 2))
                else:
                    tris.append((base + i + 1, base + i, base + i + 2))
        elif kind == "fan":
            for i in range(1, n - 1):
                tris.append((base, base + i, base + i + 1))
        elif kind == "quads":
            for i in range(0, n - 3, 4):
                tris.append((base + i, base + i + 1, base + i + 2))
                tris.append((base + i, base + i + 2, base + i + 3))


# ---------------------------------------------------------------- MAT3 / TEX1


def parse_mat3(d, off, size):
    count = u16(d, off + 8)
    ent_off = off + u32(d, off + 0x0C)
    remap_off = off + u32(d, off + 0x10)
    tex_table_off = off + u32(d, off + 0x0C + 4 * 15)
    mats = []
    for i in range(count):
        e = ent_off + u16(d, remap_off + 2 * i) * 0x14C
        tex_no_idx = u16(d, e + 0x84)
        tex = None
        if tex_no_idx != 0xFFFF and tex_table_off > off:
            tex = u16(d, tex_table_off + 2 * tex_no_idx)
        mats.append(tex)
    return mats


def parse_tex1(d, off, size):
    count = u16(d, off + 8)
    h_off = off + u32(d, off + 0x0C)
    texes = []
    for i in range(count):
        h = h_off + i * 0x20
        fmt = u8(d, h)
        w, hgt = u16(d, h + 2), u16(d, h + 4)
        wrap_s, wrap_t = u8(d, h + 6), u8(d, h + 7)
        pal_fmt = u8(d, h + 9)
        pal_count = u16(d, h + 0x0A)
        pal_off = u32(d, h + 0x0C)
        data_rel = u32(d, h + 0x1C)
        palette = None
        if pal_count:
            palette = decode_palette(d, h + pal_off, pal_count, pal_fmt)
        rgba = decode_texture(d, h + data_rel, w, hgt, fmt, palette)
        texes.append((w, hgt, rgba, wrap_s, wrap_t))
    return texes


def decode_palette(d, off, count, fmt):
    pal = []
    for i in range(count):
        v = u16(d, off + 2 * i)
        pal.append(decode_c16(v, fmt))
    return pal


def decode_c16(v, fmt):
    if fmt == 0:  # IA8
        a, i = v >> 8, v & 0xFF
        return (i, i, i, a)
    if fmt == 1:  # RGB565
        return (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31, 255)
    # RGB5A3
    if v & 0x8000:
        return (((v >> 10) & 31) * 255 // 31, ((v >> 5) & 31) * 255 // 31, (v & 31) * 255 // 31, 255)
    return (((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, ((v >> 12) & 7) * 255 // 7)


def decode_texture(d, off, w, h, fmt, palette):
    px = bytearray(w * h * 4)

    def put(x, y, r, g, b, a):
        if x < w and y < h:
            i = (y * w + x) * 4
            px[i : i + 4] = bytes((r, g, b, a))

    o = off
    if fmt == 0x0E:  # CMPR: 8x8 blocks of four 4x4 DXT1 sub-blocks
        for by in range(0, h, 8):
            for bx in range(0, w, 8):
                for sub in range(4):
                    sx = bx + (sub & 1) * 4
                    sy = by + (sub >> 1) * 4
                    c0, c1 = u16(d, o), u16(d, o + 2)
                    bits = u32(d, o + 4)
                    o += 8
                    cols = dxt1_colors(c0, c1)
                    for py in range(4):
                        for pxx in range(4):
                            sel = (bits >> (30 - 2 * (py * 4 + pxx))) & 3
                            put(sx + pxx, sy + py, *cols[sel])
        return bytes(px)

    block = {0: (8, 8), 1: (8, 4), 2: (8, 4), 3: (4, 4), 4: (4, 4), 5: (4, 4),
             6: (4, 4), 8: (8, 8), 9: (8, 4), 0xA: (4, 4)}.get(fmt, (4, 4))
    bw, bh = block
    for by in range(0, h, bh):
        for bx in range(0, w, bw):
            if fmt == 6:  # RGBA8: two cache lines (AR then GB)
                ar = d[o : o + 32]
                gb = d[o + 32 : o + 64]
                o += 64
                for py in range(4):
                    for pxx in range(4):
                        k = (py * 4 + pxx) * 2
                        put(bx + pxx, by + py, ar[k + 1], gb[k], gb[k + 1], ar[k])
                continue
            for py in range(bh):
                for pxx in range(bw):
                    if fmt == 0:  # I4
                        v = d[o + (py * bw + pxx) // 2]
                        v = (v >> 4) if pxx % 2 == 0 else (v & 0xF)
                        v *= 17
                        put(bx + pxx, by + py, v, v, v, 255)
                    elif fmt == 1:  # I8
                        v = d[o + py * bw + pxx]
                        put(bx + pxx, by + py, v, v, v, 255)
                    elif fmt == 2:  # IA4
                        v = d[o + py * bw + pxx]
                        i = (v & 0xF) * 17
                        put(bx + pxx, by + py, i, i, i, (v >> 4) * 17)
                    elif fmt == 3:  # IA8
                        v = u16(d, o + (py * bw + pxx) * 2)
                        i = v & 0xFF
                        put(bx + pxx, by + py, i, i, i, v >> 8)
                    elif fmt == 4:  # RGB565
                        v = u16(d, o + (py * bw + pxx) * 2)
                        put(bx + pxx, by + py, *decode_c16(v, 1))
                    elif fmt == 5:  # RGB5A3
                        v = u16(d, o + (py * bw + pxx) * 2)
                        put(bx + pxx, by + py, *decode_c16(v, 2))
                    elif fmt == 8:  # C4
                        v = d[o + (py * bw + pxx) // 2]
                        v = (v >> 4) if pxx % 2 == 0 else (v & 0xF)
                        c = palette[v] if palette and v < len(palette) else (255, 0, 255, 255)
                        put(bx + pxx, by + py, *c)
                    elif fmt == 9:  # C8
                        v = d[o + py * bw + pxx]
                        c = palette[v] if palette and v < len(palette) else (255, 0, 255, 255)
                        put(bx + pxx, by + py, *c)
            sizes = {0: 32, 1: 32, 2: 32, 3: 32, 4: 32, 5: 32, 8: 32, 9: 32, 0xA: 32}
            o += sizes.get(fmt, 32)
    return bytes(px)


def dxt1_colors(c0, c1):
    r0, g0, b0, _ = decode_c16(c0, 1)
    r1, g1, b1, _ = decode_c16(c1, 1)
    if c0 > c1:
        return [
            (r0, g0, b0, 255),
            (r1, g1, b1, 255),
            ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3, 255),
            ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3, 255),
        ]
    return [
        (r0, g0, b0, 255),
        (r1, g1, b1, 255),
        ((r0 + r1) // 2, (g0 + g1) // 2, (b0 + b1) // 2, 255),
        (0, 0, 0, 0),
    ]


# ---------------------------------------------------------------- PNG


def write_png(path, w, h, rgba):
    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + rgba[y * w * 4 : (y + 1) * w * 4] for y in range(h))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    Path(path).write_bytes(png)


# ---------------------------------------------------------------- glTF


def convert(bmd_data, out_dir, name):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    chunks = find_chunks(bmd_data)
    d = bmd_data

    arrays = parse_vtx1(d, *chunks["VTX1"])
    joints = parse_jnt1(d, *chunks["JNT1"]) if "JNT1" in chunks else []
    shape_mat, joint_parents = parse_inf1(d, *chunks["INF1"])
    envelopes, inv_binds = parse_evp1(d, *chunks["EVP1"]) if "EVP1" in chunks else ([], {})
    drw1 = parse_drw1(d, *chunks["DRW1"]) if "DRW1" in chunks else []
    mat_tex = parse_mat3(d, *chunks["MAT3"]) if "MAT3" in chunks else []
    texes = parse_tex1(d, *chunks["TEX1"]) if "TEX1" in chunks else []

    # joint global matrices (bind pose)
    globals_ = []
    for i, j in enumerate(joints):
        local = joint_local_matrix(j)
        p = joint_parents.get(i)
        globals_.append(mat_mul(globals_[p], local) if p is not None else local)

    # DRW1 entry -> baked 4x4 draw matrix
    draw_matrices = []
    for weighted, idx in drw1:
        if not weighted:
            draw_matrices.append(globals_[idx] if idx < len(globals_) else None)
        else:
            acc = [[0.0] * 4 for _ in range(4)]
            for ji, w in envelopes[idx]:
                m = mat_mul(globals_[ji], inv_binds[ji])
                for r in range(4):
                    for c in range(4):
                        acc[r][c] += m[r][c] * w
            draw_matrices.append(acc)
    if not draw_matrices:
        draw_matrices = [globals_[0] if globals_ else None]

    shapes = parse_shp1(d, *chunks["SHP1"], arrays, draw_matrices)

    # textures to PNG
    tex_files = []
    for i, (w, h, rgba, ws, wt) in enumerate(texes):
        fn = f"{name}_tex{i}.png"
        write_png(out / fn, w, h, rgba)
        tex_files.append((fn, ws, wt))

    # build glTF
    bin_parts = []
    accessors = []
    views = []
    meshes_prims = []
    bin_len = 0

    def add_view(data, target):
        nonlocal bin_len
        views.append({"buffer": 0, "byteOffset": bin_len, "byteLength": len(data), "target": target})
        bin_parts.append(data)
        bin_len += len(data) + ((4 - len(data) % 4) % 4)
        bin_parts.append(b"\x00" * ((4 - len(data) % 4) % 4))
        return len(views) - 1

    for si, (verts, tris) in enumerate(shapes):
        if not tris:
            continue
        pos = b"".join(struct.pack("<3f", *v[0]) for v in verts)
        mins = [min(v[0][i] for v in verts) for i in range(3)]
        maxs = [max(v[0][i] for v in verts) for i in range(3)]
        attrs = {"POSITION": len(accessors)}
        accessors.append({"bufferView": add_view(pos, 34962), "componentType": 5126,
                          "count": len(verts), "type": "VEC3", "min": mins, "max": maxs})
        if all(v[1] is not None for v in verts):
            nrm = b"".join(struct.pack("<3f", *v[1]) for v in verts)
            attrs["NORMAL"] = len(accessors)
            accessors.append({"bufferView": add_view(nrm, 34962), "componentType": 5126,
                              "count": len(verts), "type": "VEC3"})
        if all(v[2] is not None for v in verts):
            uv = b"".join(struct.pack("<2f", v[2][0], v[2][1]) for v in verts)
            attrs["TEXCOORD_0"] = len(accessors)
            accessors.append({"bufferView": add_view(uv, 34962), "componentType": 5126,
                              "count": len(verts), "type": "VEC2"})
        if all(v[3] is not None for v in verts):
            col = b"".join(struct.pack("<4f", *v[3]) for v in verts)
            attrs["COLOR_0"] = len(accessors)
            accessors.append({"bufferView": add_view(col, 34962), "componentType": 5126,
                              "count": len(verts), "type": "VEC4"})
        idx = b"".join(struct.pack("<3I", *t) for t in tris)
        idx_acc = len(accessors)
        accessors.append({"bufferView": add_view(idx, 34963), "componentType": 5125,
                          "count": len(tris) * 3, "type": "SCALAR"})
        meshes_prims.append({"attributes": attrs, "indices": idx_acc,
                             "material": shape_mat.get(si, 0)})

    materials = []
    gl_textures = []
    samplers = []
    images = []
    wrap_gl = {0: 33071, 1: 10497, 2: 33648}
    for mi in range(max(len(mat_tex), 1)):
        ti = mat_tex[mi] if mi < len(mat_tex) else None
        mat = {"name": f"mat{mi}", "doubleSided": True,
               "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0},
               "alphaMode": "MASK", "alphaCutoff": 0.5}
        if ti is not None and ti < len(tex_files):
            fn, ws, wt = tex_files[ti]
            img_i = len(images)
            images.append({"uri": fn})
            samplers.append({"wrapS": wrap_gl.get(ws, 10497), "wrapT": wrap_gl.get(wt, 10497),
                             "magFilter": 9729, "minFilter": 9987})
            gl_textures.append({"source": img_i, "sampler": len(samplers) - 1})
            mat["pbrMetallicRoughness"]["baseColorTexture"] = {"index": len(gl_textures) - 1}
        materials.append(mat)

    gltf = {
        "asset": {"version": "2.0", "generator": "dusklight bmd2gltf"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"primitives": meshes_prims, "name": name}],
        "materials": materials,
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"uri": f"{name}.bin", "byteLength": bin_len}],
    }
    if gl_textures:
        gltf["textures"] = gl_textures
        gltf["samplers"] = samplers
        gltf["images"] = images

    (out / f"{name}.bin").write_bytes(b"".join(bin_parts))
    (out / f"{name}.gltf").write_text(json.dumps(gltf))
    return len(meshes_prims), len(texes)


if __name__ == "__main__":
    data = Path(sys.argv[1]).read_bytes()
    n_prims, n_tex = convert(data, sys.argv[2], Path(sys.argv[1]).stem)
    print(f"{sys.argv[1]}: {n_prims} primitives, {n_tex} textures")
