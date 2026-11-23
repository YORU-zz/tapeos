#!/usr/bin/env python3
"""Insert several files into a blank full-format TapeOS
(TOSD) D88 image at once.

tapeos/tools/putfile.py writes a fresh directory holding
exactly one entry and refuses a non-blank directory, which
is the safe behaviour for its one-file use; the stage-2 dev
disk needs three files (EDAS + two source texts), so this
sibling writes them in one pass with the same on-disk
layout: directory sector = record 32 ("TOSD", version 1,
next-free at +6, 16-byte entries from +16: name 10
space-padded / start record LE16 / size LE16 / load address
LE16), file data contiguous from record 48.

usage: putfiles.py <image.d88> <file> <NAME> <loadaddr>
                    [<file> <NAME> <loadaddr> ...]
"""
import struct
import sys

D88_HEADER_SIZE = 0x2B0
DIRREC = 32
DATAREC0 = 48


def record_map(img):
    offsets = {}
    for unit in range(164):
        base = struct.unpack_from("<I", img, 0x20 + 4 * unit)[0]
        if base == 0:
            continue
        pos = base
        for _ in range(16):
            c, h, r, _n = struct.unpack_from("<BBBB", img, pos)
            dsize = struct.unpack_from("<H", img, pos + 14)[0]
            offsets[(c * 2 + h) * 16 + (r - 1)] = pos + 16
            pos += 16 + dsize
    return offsets


def main():
    img_path = sys.argv[1]
    specs = []
    args = sys.argv[2:]
    assert len(args) % 3 == 0, "arguments must be file/NAME/load triples"
    for i in range(0, len(args), 3):
        specs.append((args[i], args[i + 1].upper(), int(args[i + 2], 0)))
    img = bytearray(open(img_path, "rb").read())
    offs = record_map(img)
    if DIRREC not in offs:
        raise SystemExit("directory record not materialised "
                         "(use a 'full' mkd88 image)")
    diroff = offs[DIRREC]
    if any(img[diroff:diroff + 256]):
        raise SystemExit("directory sector is not blank; refusing")

    dsec = bytearray(256)
    dsec[0:4] = b"TOSD"
    dsec[4] = 1
    rec = DATAREC0
    for idx, (path, name, load) in enumerate(specs):
        assert 1 <= len(name) <= 10, f"name {name!r} must be 1..10 chars"
        data = open(path, "rb").read()
        nrec = (len(data) + 255) // 256
        for i in range(nrec):
            if rec + i not in offs:
                raise SystemExit(f"record {rec + i} not materialised")
            chunk = data[i * 256:(i + 1) * 256].ljust(256, b"\0")
            o = offs[rec + i]
            img[o:o + 256] = chunk
        eoff = 16 + 16 * idx
        dsec[eoff:eoff + 10] = name.encode().ljust(10, b" ")
        struct.pack_into("<H", dsec, eoff + 10, rec)
        struct.pack_into("<H", dsec, eoff + 12, len(data))
        struct.pack_into("<H", dsec, eoff + 14, load)
        print(f"{img_path}: file '{name}' ({len(data)} bytes, load "
              f"0x{load:04x}) at records {rec}..{rec + nrec - 1}")
        rec += nrec
    struct.pack_into("<H", dsec, 6, rec)          # next free
    img[diroff:diroff + 256] = dsec
    with open(img_path, "wb") as f:
        f.write(img)
    print(f"next free record = {rec}")


if __name__ == "__main__":
    main()
