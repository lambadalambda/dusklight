"""Nintendo GC resource container parsing: Yaz0 decompression and RARC archives."""
import struct


def yaz0(data: bytes) -> bytes:
    if data[:4] != b"Yaz0":
        return data
    size = struct.unpack(">I", data[4:8])[0]
    out = bytearray()
    src = 16
    code, bits = 0, 0
    while len(out) < size:
        if bits == 0:
            code = data[src]
            src += 1
            bits = 8
        if code & 0x80:
            out.append(data[src])
            src += 1
        else:
            b1, b2 = data[src], data[src + 1]
            src += 2
            dist = ((b1 & 0xF) << 8) | b2
            copy = b1 >> 4
            if copy == 0:
                copy = data[src] + 0x12
                src += 1
            else:
                copy += 2
            pos = len(out) - dist - 1
            for _ in range(copy):
                out.append(out[pos])
                pos += 1
        code <<= 1
        bits -= 1
    return bytes(out)


def rarc_files(d: bytes):
    """Yield (name, content) for every file in a RARC archive."""
    assert d[:4] == b"RARC", "not a RARC archive"
    data_off = struct.unpack(">I", d[12:16])[0] + 0x20
    num_nodes, node_off, num_ents, ent_off = struct.unpack(">IIII", d[0x20:0x30])
    str_off = struct.unpack(">I", d[0x34:0x38])[0] + 0x20
    ent_off += 0x20
    for i in range(num_ents):
        e = ent_off + i * 0x14
        _, _, ftype_name = struct.unpack(">HHI", d[e : e + 8])
        name_off = ftype_name & 0xFFFFFF
        ftype = ftype_name >> 24
        off, size = struct.unpack(">II", d[e + 8 : e + 16])
        ns = str_off + name_off
        name = d[ns : d.index(b"\0", ns)].decode("ascii", "replace")
        if ftype & 0x01:  # file (not directory)
            yield name, d[data_off + off : data_off + off + size]
