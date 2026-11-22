#!/usr/bin/env python3
"""Insert a file into a TapeOS (TOSD) D88 disk image.

The image must be a full-format one (all 80 track/side units
materialised, as produced by `mkd88.py ... full`) whose file
area is still blank. The tool writes a fresh TOSD directory
sector (record 32) holding exactly one entry, and the file
data at records 48.. (contiguous), setting the directory's
next-free-record past the file. Refuses to run if the
directory sector is not blank, so it cannot clobber a disk
that already carries files.

Disk layout (see README / src/tapeos.z80): record =
((track * 2 + side) << 4) | (sector - 1), 256 bytes each;
record 32 = directory ("TOSD", version 1, entries of 16 bytes
from +16: name 10 space-padded / start record LE16 / size
LE16 / load address LE16); file data starts at record 48.

usage: putfont.py <image.d88> <file.bin> <NAME>
       (NAME: 1..10 chars, stored upper-case, space padded)
"""
import struct
import sys

D88_HEADER_SIZE = 0x2B0
DIRREC = 32
DATAREC0 = 48


def record_map(img):
    """record number -> absolute offset of its 256 data bytes."""
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
    img_path, file_path, name = sys.argv[1], sys.argv[2], sys.argv[3]
    name = name.upper()
    assert 1 <= len(name) <= 10, "name must be 1..10 chars"
    img = bytearray(open(img_path, "rb").read())
    data = open(file_path, "rb").read()
    offs = record_map(img)

    nrec = (len(data) + 255) // 256
    need = [DIRREC] + [DATAREC0 + i for i in range(nrec)]
    for rec in need:
        if rec not in offs:
            raise SystemExit(f"record {rec} not materialised in image "
                             "(use a 'full' mkd88 image)")

    diroff = offs[DIRREC]
    if any(img[diroff:diroff + 256]):
        raise SystemExit("directory sector is not blank; refusing "
                         "to overwrite an existing TOSD directory")

    # file data
    for i in range(nrec):
        chunk = data[i * 256:(i + 1) * 256].ljust(256, b"\0")
        o = offs[DATAREC0 + i]
        img[o:o + 256] = chunk

    # directory sector
    dsec = bytearray(256)
    dsec[0:4] = b"TOSD"
    dsec[4] = 1                                   # format version
    struct.pack_into("<H", dsec, 6, DATAREC0 + nrec)   # next free
    entry = name.encode().ljust(10, b" ")
    dsec[16:26] = entry
    struct.pack_into("<H", dsec, 26, DATAREC0)         # start record
    struct.pack_into("<H", dsec, 28, len(data))        # size
    struct.pack_into("<H", dsec, 30, 0)                # load address
    img[diroff:diroff + 256] = dsec

    with open(img_path, "wb") as f:
        f.write(img)
    print(f"{img_path}: file '{name}' ({len(data)} bytes) at records "
          f"{DATAREC0}..{DATAREC0 + nrec - 1}, next free = "
          f"{DATAREC0 + nrec}")


if __name__ == "__main__":
    main()
