#!/usr/bin/env python3
"""Build a CMT tape image (.cmt) for the compatible IPL + the
patched xmil core (TapeOS M3).

The .cmt file is a raw packed bitstream (MSB first), one bit per
tape bit cell. The core's CMT player (io/cmt.c) turns each bit
into a pulse: bit 1 = high 380us + low 280us, bit 0 = high 100us
+ low 280us. The IPL reader samples ~185us after each rising
edge, so a long pulse reads as 1 and a short one as 0.

Stream layout (from the compatible IPL source, CMT_LOADIFB /
CMT_LOADFILE / MT_RDBYTE):
  info block:
    800 x '1'          leader (IPL searches >=255 consecutive 1s,
                       then skips 1s until the first 0)
    40 x '0'
    41 x '1'
    34 bytes           32-byte info block + 2 checksum bytes,
                       each byte sent as: start bit (1), then
                       8 data bits, MSB first
  data block:
    800 x '1'          leader (same search)
    20 x '0'
    21 x '1'
    N bytes            program body, same byte encoding,
                       N = size field of the info block

Info block layout = the HuBASIC header used by the FDD path
(see mkd88.py): flag 0x01, name 13B, ext 3B, password, size
LE16, load LE16, exec LE16, date 6B, offset 3B (unused on tape).
Checksum = LE16 byte-sum of the 32 info bytes (the IPL reads
the 2 checksum bytes but does not verify them).

usage: mkcmt.py program.bin out.cmt [loadaddr] [execaddr]
"""
import struct
import sys


def byte_bits(b):
    """One byte on tape: start bit (1) + 8 data bits, MSB first."""
    return [1] + [(b >> (7 - i)) & 1 for i in range(8)]


def main():
    prog_path, out_path = sys.argv[1], sys.argv[2]
    load_addr = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x2000
    exec_addr = int(sys.argv[4], 0) if len(sys.argv) > 4 else load_addr

    with open(prog_path, 'rb') as f:
        prog = f.read()

    # ---- 32-byte info block (HuBASIC header layout) ----
    info = bytearray(32)
    info[0x00] = 0x01                                # boot flag
    info[0x01:0x0E] = b'TAPEOS'.ljust(13, b' ')
    info[0x0E:0x11] = b'Sys'                         # extension
    info[0x11] = 0                                   # password
    struct.pack_into('<H', info, 0x12, len(prog))    # size
    struct.pack_into('<H', info, 0x14, load_addr)    # load address
    struct.pack_into('<H', info, 0x16, exec_addr)    # exec address
    # +0x18 date and +0x1D offset: leave zero (unused on tape)
    cksum = sum(info) & 0xFFFF
    info34 = bytes(info) + struct.pack('<H', cksum)

    # ---- bitstream ----
    bits = []
    bits += [1] * 800 + [0] * 40 + [1] * 41          # info leader
    for b in info34:
        bits += byte_bits(b)
    bits += [1] * 800 + [0] * 20 + [1] * 21          # data leader
    for b in prog:
        bits += byte_bits(b)

    # ---- pack MSB first, pad the tail with 1s ----
    while len(bits) % 8:
        bits.append(1)
    out = bytearray()
    for i in range(0, len(bits), 8):
        v = 0
        for bit in bits[i:i + 8]:
            v = (v << 1) | bit
        out.append(v)

    with open(out_path, 'wb') as f:
        f.write(out)
    print(f'{out_path}: {len(out)} bytes ({len(bits)} bits), '
          f'program {len(prog)} bytes @ load 0x{load_addr:04x} '
          f'exec 0x{exec_addr:04x}, info cksum 0x{cksum:04x}')


if __name__ == '__main__':
    main()
