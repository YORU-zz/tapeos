#!/usr/bin/env python3
"""Build a bootable X1 2D D88 disk image for the compatible IPL.

Boot format (from the X1 compatible ROM source, meister68k, CC0):
  - The IPL reads track 0 / side 0 / sector 1 (256 bytes) into 0xFE00.
  - Byte +0x00 must be 0x01 (boot flag).
  - Header layout in that sector (HuBASIC-style):
        +0x00 boot flag (1 byte)
        +0x01 file name (13 bytes)
        +0x0e extension (3 bytes)
        +0x11 password (1 byte, ignored)
        +0x12 program size in bytes (LE16)
        +0x14 load address (LE16)
        +0x16 exec address (LE16)
        +0x18 date (6 bytes, ignored)
        +0x1d disk offset (3 bytes): byte offset of the program data.
              For 256-byte-sector FDD the low byte is 0 and the upper
              two bytes are the starting record number, where
              record = ((track * 2 + side) << 4) | (sector - 1).
  - The IPL then loads `size` bytes (rounded up to whole sectors)
    starting at that record, and jumps to the exec address after
    disconnecting the IPL ROM.

Layout produced here: record 0 = header sector, record 1.. = program.
Records run consecutively across sides and tracks
(record = ((track * 2 + side) << 4) | (sector - 1)), so programs
larger than one side (4096 bytes) spill onto track 0 / side 1,
track 1 / side 0, and so on. Only the (track, side) units that
hold data are emitted. The size field is LE16, so the program
limit is 65535 bytes.

usage: mkd88.py program.bin out.d88 [loadaddr] [execaddr] [full]

With "full" as the 5th argument, all 80 track/side units are
emitted (sectors beyond the program are zero-filled), giving a
fully formatted 320 KB image whose every sector the core's
D88 driver can read and write (M4: the TapeOS FD file area).
"""
import struct
import sys

SECTORS_PER_TRACK = 16
SECTOR_SIZE = 256
D88_HEADER_SIZE = 0x2B0


def sector_block(c, h, r, data):
    hdr = struct.pack(
        '<BBBBHBBBBxxxBH',
        c, h, r, 1,               # C, H, R, N (N=1 -> 256 bytes)
        SECTORS_PER_TRACK,        # sectors in this track
        1,                        # MFM flag
        0,                        # deleted flag
        0,                        # status (FDC result: normal)
        0,                        # seek time (reserved area)
        0,                        # rpm flag (reserved area)
        len(data),                # data size
    )
    assert len(hdr) == 16, len(hdr)
    return hdr + data


def main():
    prog_path, out_path = sys.argv[1], sys.argv[2]
    load_addr = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x2000
    exec_addr = int(sys.argv[4], 0) if len(sys.argv) > 4 else load_addr
    full = len(sys.argv) > 5 and sys.argv[5] == 'full'

    with open(prog_path, 'rb') as f:
        prog = f.read()
    if len(prog) > 0xFFFF:
        raise SystemExit('program too large (size field is LE16)')

    # ---- header sector (record 0) ----
    header = bytearray(SECTOR_SIZE)
    header[0x00] = 0x01                                # boot flag
    name = b'TAPEOS'.ljust(13, b' ')
    header[0x01:0x0E] = name
    header[0x0E:0x11] = b'Sys'                         # extension
    header[0x11] = 0                                   # password
    struct.pack_into('<H', header, 0x12, len(prog))    # size
    struct.pack_into('<H', header, 0x14, load_addr)    # load address
    struct.pack_into('<H', header, 0x16, exec_addr)    # exec address
    # +0x18 date: leave zero
    header[0x1D] = 0                                   # offset low byte
    struct.pack_into('<H', header, 0x1E, 1)            # start record = 1

    # ---- records 0.. : header sector, then program sectors ----
    padded = prog.ljust(
        (len(prog) + SECTOR_SIZE - 1) // SECTOR_SIZE * SECTOR_SIZE, b'\0')
    n_prog_sectors = len(padded) // SECTOR_SIZE
    n_records = 1 + n_prog_sectors
    n_units = (n_records + SECTORS_PER_TRACK - 1) // SECTORS_PER_TRACK
    if full:
        n_units = 80                    # 40 tracks x 2 sides

    body = bytearray()
    table = [0] * 164
    for unit in range(n_units):
        track_no, side = unit >> 1, unit & 1
        table[unit] = D88_HEADER_SIZE + len(body)
        for s in range(SECTORS_PER_TRACK):
            record = unit * SECTORS_PER_TRACK + s
            if record == 0:
                data = bytes(header)
            elif record - 1 < n_prog_sectors:
                data = padded[(record - 1) * SECTOR_SIZE:
                              record * SECTOR_SIZE]
            else:
                data = bytes(SECTOR_SIZE)
            body += sector_block(track_no, side, s + 1, data)

    # ---- D88 container ----
    total = D88_HEADER_SIZE + len(body)
    head = bytearray()
    head += b'TAPEOS_M1'.ljust(17, b'\0')              # disk name
    head += bytes(9)                                   # reserved
    head += bytes([0])                                 # write protect: no
    head += bytes([0x00])                              # fd_type: 2D
    head += struct.pack('<I', total)                   # disk size
    for off in table:
        head += struct.pack('<I', off)
    assert len(head) == D88_HEADER_SIZE

    with open(out_path, 'wb') as f:
        f.write(head)
        f.write(body)
    print(f'{out_path}: {total} bytes, program {len(prog)} bytes '
          f'@ load 0x{load_addr:04x} exec 0x{exec_addr:04x} '
          f'({n_units} track-side unit(s))')


if __name__ == '__main__':
    main()
