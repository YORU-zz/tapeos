#!/usr/bin/env python3
"""Build a Tiny Scheme boot tape (.cmt) for the X1 core.

The wire format is the compatible IPL / TapeOS format (see
tapeos/tools/mkcmt.py): the .cmt is a raw packed bitstream,
MSB first; a byte on tape is a start bit (1) plus 8 data
bits, MSB first; an info block is 800x1 leader, 40x0, 41x1,
then 34 framed bytes (32-byte HuBASIC-style info + LE16
byte-sum); a data block is 800x1, 20x0, 21x1, then the
payload bytes (no trailing checksum).

Info block: +0 flag, +1..13 name (space padded), +14..16
ext, +17 password(0), +18 size LE16, +20 load LE16,
+22 exec LE16, rest zero.

Tape layout:
  file 1  TSLOAD   the stage-1 loader (flag 1, IPL-loadable)
  file 2  SCRIPT   the fixed-length script block (flag 0):
                   4096-byte payload = 8-byte header
                   ("TS", ver, len LE16, byte-sum LE16)
                   + Scheme text + zero padding
  then     the loadable files given with --file (flag 1)

Options:
  --loader BIN --script SCM --out CMT
  --file NAME:EXT:LOADHEX:BIN   (repeatable)
  --corrupt-script              flip one script text byte
                                after the checksum is taken
                                (fallback-path test tape)
"""
import argparse
import struct

LEADER_BITS = 800
GAP_ZEROS = 2000
SCRIPT_PAYLOAD = 4096


def byte_bits(b):
    return [1] + [(b >> (7 - i)) & 1 for i in range(8)]


def encode_file(bits, name, ext, flag, load, exec_, data):
    info = bytearray(32)
    info[0] = flag
    info[1:14] = name.encode("ascii").ljust(13, b" ")
    info[14:17] = ext.encode("ascii").ljust(3, b" ")
    struct.pack_into("<H", info, 18, len(data))
    struct.pack_into("<H", info, 20, load)
    struct.pack_into("<H", info, 22, exec_)
    cks = sum(info) & 0xFFFF
    info34 = bytes(info) + struct.pack("<H", cks)
    bits += [1] * LEADER_BITS + [0] * 40 + [1] * 41
    for b in info34:
        bits += byte_bits(b)
    bits += [1] * LEADER_BITS + [0] * 20 + [1] * 21
    for b in data:
        bits += byte_bits(b)
    bits += [0] * GAP_ZEROS


def script_payload(text, corrupt=False):
    if len(text) > SCRIPT_PAYLOAD - 8:
        raise SystemExit("script too long: %d > %d"
                         % (len(text), SCRIPT_PAYLOAD - 8))
    payload = bytearray(SCRIPT_PAYLOAD)
    payload[0:2] = b"TS"
    payload[2] = 1
    struct.pack_into("<H", payload, 4, len(text))
    struct.pack_into("<H", payload, 6, sum(text) & 0xFFFF)
    payload[8:8 + len(text)] = text
    if corrupt:
        payload[100] ^= 0xFF
    return bytes(payload)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--loader", required=True)
    ap.add_argument("--script", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--file", action="append", default=[])
    ap.add_argument("--corrupt-script", action="store_true")
    args = ap.parse_args()

    loader = open(args.loader, "rb").read()
    text = open(args.script, "rb").read()
    bits = []
    encode_file(bits, "TSLOAD", "Sys", 1, 0x2000, 0x2000, loader)
    encode_file(bits, "SCRIPT", "TS ", 0, 0, 0,
                script_payload(text, args.corrupt_script))
    for spec in args.file:
        name, ext, loadh, path = spec.split(":")
        data = open(path, "rb").read()
        load = int(loadh, 16)
        encode_file(bits, name, ext, 1, load, load, data)
    while len(bits) % 8:
        bits.append(1)
    out = bytearray()
    for i in range(0, len(bits), 8):
        v = 0
        for bit in bits[i:i + 8]:
            v = (v << 1) | bit
        out.append(v)
    open(args.out, "wb").write(bytes(out))
    print("%s: %d bits, loader %d bytes, script %d bytes"
          % (args.out, len(bits), len(loader), len(text)))


if __name__ == "__main__":
    main()
