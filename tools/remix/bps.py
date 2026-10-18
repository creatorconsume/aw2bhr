"""BPS patch encoder / decoder (beat patch format, as used by Flips, beat, Rom Patcher JS, ...).

Pure stdlib.  Format (all integers are BPS varints):
  "BPS1" source_size target_size metadata_size metadata
  actions: varint((length-1) << 2 | kind) [+ varint(signed offset) for kinds 2/3]
     0 SourceRead  copy `length` bytes from source at the current output position
     1 TargetRead  `length` literal bytes follow
     2 SourceCopy  copy from source at sourceRelativeOffset (+= signed delta)
     3 TargetCopy  copy from already-written target at targetRelativeOffset (+= signed delta)
  footer: crc32(source) crc32(target) crc32(patch bytes before this field), little endian u32

usage:
  python3 -I bps.py create SOURCE TARGET PATCH.bps
  python3 -I bps.py apply  SOURCE PATCH.bps OUT
  python3 -I bps.py info   PATCH.bps
"""
import struct
import sys
import zlib

SOURCE_READ, TARGET_READ, SOURCE_COPY, TARGET_COPY = range(4)


def _crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def enc_varint(n):
    assert n >= 0
    out = bytearray()
    while True:
        x = n & 0x7F
        n >>= 7
        if n == 0:
            out.append(0x80 | x)
            return bytes(out)
        out.append(x)
        n -= 1


def dec_varint(buf, p):
    data, shift = 0, 1
    while True:
        x = buf[p]
        p += 1
        data += (x & 0x7F) * shift
        if x & 0x80:
            return data, p
        shift <<= 7
        data += shift


def _signed(n):
    return enc_varint((abs(n) << 1) | (1 if n < 0 else 0))


def encode(source, target, metadata=b"", min_match=4):
    """Build a BPS patch turning `source` into `target`.

    Linear diff, which suits ROM hacks (data patched in place or written into free space):
    SourceRead for runs where target == source at the same offset (>= min_match bytes, or the
    tail), TargetRead literals for everything else.  The decoder below implements all four
    actions, so patches from other tools (Flips etc.) also apply.
    """
    source, target = bytes(source), bytes(target)
    out = bytearray(b"BPS1")
    out += enc_varint(len(source)) + enc_varint(len(target)) + enc_varint(len(metadata)) + metadata
    n, ns = len(target), len(source)
    lim = min(n, ns)

    def same_run(pos):
        """length of the run where target[pos:] == source[pos:]"""
        e = pos
        step = 4096
        while e + step <= lim and target[e:e + step] == source[e:e + step]:
            e += step
        while e < lim and target[e] == source[e]:
            e += 1
        return e - pos

    i = 0
    literal = bytearray()

    def flush_literal():
        if literal:
            out.extend(enc_varint(((len(literal) - 1) << 2) | TARGET_READ))
            out.extend(literal)
            literal.clear()

    while i < n:
        run = same_run(i) if i < lim else 0
        if run >= min_match or (run and i + run == n):
            flush_literal()
            out.extend(enc_varint(((run - 1) << 2) | SOURCE_READ))
            i += run
            continue
        if run:                      # short match inside a changed region: keep as literal
            literal += target[i:i + run]
            i += run
            continue
        literal.append(target[i])
        i += 1
    flush_literal()
    out += struct.pack("<II", _crc(source), _crc(target))
    out += struct.pack("<I", _crc(out))
    return bytes(out)


class BPSError(Exception):
    pass


def decode(source, patch, check_crc=True):
    """Apply BPS `patch` to `source`; returns target bytes. Raises BPSError on any mismatch."""
    patch = bytes(patch)
    if patch[:4] != b"BPS1":
        raise BPSError("not a BPS1 patch")
    if check_crc and _crc(patch[:-4]) != struct.unpack_from("<I", patch, len(patch) - 4)[0]:
        raise BPSError("patch crc mismatch")
    p = 4
    ssize, p = dec_varint(patch, p)
    tsize, p = dec_varint(patch, p)
    msize, p = dec_varint(patch, p)
    p += msize
    if ssize != len(source):
        raise BPSError("source size %d != expected %d" % (len(source), ssize))
    if check_crc and _crc(source) != struct.unpack_from("<I", patch, len(patch) - 12)[0]:
        raise BPSError("source crc mismatch")
    out = bytearray()
    src_rel = tgt_rel = 0
    end = len(patch) - 12
    while p < end:
        d, p = dec_varint(patch, p)
        kind, length = d & 3, (d >> 2) + 1
        if kind == SOURCE_READ:
            o = len(out)
            out += source[o:o + length]
        elif kind == TARGET_READ:
            out += patch[p:p + length]
            p += length
        else:
            d, p = dec_varint(patch, p)
            delta = -(d >> 1) if d & 1 else d >> 1
            if kind == SOURCE_COPY:
                src_rel += delta
                out += source[src_rel:src_rel + length]
                src_rel += length
            else:
                tgt_rel += delta
                for _ in range(length):     # may overlap
                    out.append(out[tgt_rel])
                    tgt_rel += 1
    if len(out) != tsize:
        raise BPSError("target size %d != expected %d" % (len(out), tsize))
    if check_crc and _crc(out) != struct.unpack_from("<I", patch, len(patch) - 8)[0]:
        raise BPSError("target crc mismatch")
    return bytes(out)


def info(patch):
    p = 4
    ssize, p = dec_varint(patch, p)
    tsize, p = dec_varint(patch, p)
    msize, p = dec_varint(patch, p)
    meta = patch[p:p + msize]
    s, t, c = struct.unpack_from("<III", patch, len(patch) - 12)
    return {"source_size": ssize, "target_size": tsize, "metadata": meta, "source_crc": s,
            "target_crc": t, "patch_crc": c, "patch_size": len(patch)}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "create" and len(argv) == 5:
        src, tgt = open(argv[2], "rb").read(), open(argv[3], "rb").read()
        patch = encode(src, tgt)
        assert decode(src, patch) == tgt
        open(argv[4], "wb").write(patch)
        print("wrote %s (%d bytes)" % (argv[4], len(patch)))
    elif cmd == "apply" and len(argv) == 5:
        src, patch = open(argv[2], "rb").read(), open(argv[3], "rb").read()
        open(argv[4], "wb").write(decode(src, patch))
        print("wrote", argv[4])
    elif cmd == "info" and len(argv) == 3:
        i = info(open(argv[2], "rb").read())
        print("\n".join("%s: %s" % (k, ("0x%08X" % v) if k.endswith("crc") else v) for k, v in i.items()))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
