#!/usr/bin/env python3
"""Parse an X1 classic-speed .cmt bitstream and list files.

Mirrors the IPL / TapeOS reader semantics (see
tapeos/tools/mkcmt.py): bits are packed MSB first; a byte
is a start bit (1) plus 8 data bits MSB first; the leader
search finds >=255 ones and skips ones up to the first 0
(that 0 is the first of the 40/20 zeros), then 41/21 ones.
The info block is 32 bytes + LE16 byte-sum; the data block
has no trailing checksum.  For the SCRIPT file the
fixed-length script block header is validated too.

usage: lstape.py FILE.cmt [--expect-script-text TEXTFILE]
Exit status 0 on a clean parse (and matching script text
when requested), 1 otherwise.
"""
import struct
import sys


def bits_of(data):
    bits = []
    for byte in data:
        for k in range(8):
            bits.append((byte >> (7 - k)) & 1)
    return bits


class Reader:
    def __init__(self, bits):
        self.bits = bits
        self.i = 0

    def leader(self):
        # find a run of >=255 ones, then skip ones up to and
        # including the first 0 (consumed as sync zero #1)
        run = 0
        while self.i < len(self.bits):
            b = self.bits[self.i]
            self.i += 1
            if b:
                run += 1
            else:
                if run >= 255:
                    return True
                run = 0
        return False

    def sync(self, total_zeros):
        # leader() already consumed the first zero; the rest
        # of the run has an exact length (the ones run is
        # followed by a framed start bit, also 1, so counting
        # must stop at the exact total)
        for _ in range(total_zeros - 1):
            if self.i >= len(self.bits) or self.bits[self.i] != 0:
                raise ValueError("bad sync: zero run short")
            self.i += 1
        for _ in range(total_zeros + 1):
            if self.i >= len(self.bits) or self.bits[self.i] != 1:
                raise ValueError("bad sync: one run short")
            self.i += 1

    def byte(self):
        if self.i + 9 > len(self.bits):
            raise EOFError
        start = self.bits[self.i]
        self.i += 1
        if start != 1:
            raise ValueError("bad start bit at %d" % self.i)
        v = 0
        for _ in range(8):
            v = (v << 1) | self.bits[self.i]
            self.i += 1
        return v

    def block(self, n):
        return bytes(self.byte() for _ in range(n))


def parse(path):
    bits = bits_of(open(path, "rb").read())
    r = Reader(bits)
    files = []
    while True:
        if not r.leader():
            break
        r.sync(40)
        info34 = r.block(34)
        info = info34[:32]
        ck = struct.unpack_from("<H", info34, 32)[0]
        if ck != (sum(info) & 0xFFFF):
            raise ValueError("info checksum mismatch")
        name = info[1:14].decode("ascii", "replace")
        ext = info[14:17].decode("ascii", "replace")
        flag = info[0]
        size, load, exec_ = struct.unpack_from("<HHH", info, 18)
        if not r.leader():
            raise ValueError("no data leader for " + name)
        r.sync(20)
        data = r.block(size)
        files.append({"name": name, "ext": ext, "flag": flag,
                      "load": load, "exec": exec_, "size": size,
                      "data": data})
    return files


def check_script(f):
    d = f["data"]
    if len(d) != 4096:
        return "script payload size %d != 4096" % len(d)
    if d[0:2] != b"TS":
        return "script magic missing"
    ln = d[4] | d[5] << 8
    ck = d[6] | d[7] << 8
    if ck != (sum(d[8:8 + ln]) & 0xFFFF):
        return "script checksum mismatch"
    return None


def main():
    path = sys.argv[1]
    expect = None
    if "--expect-script-text" in sys.argv:
        expect = open(sys.argv[sys.argv.index(
            "--expect-script-text") + 1], "rb").read()
    try:
        files = parse(path)
    except (ValueError, EOFError) as e:
        print("lstape: FAIL:", e)
        return 1
    for i, f in enumerate(files):
        print("%d: %s.%s flag=%d load=%04X exec=%04X size=%d"
              % (i + 1, f["name"].strip(), f["ext"].strip(),
                 f["flag"], f["load"], f["exec"], f["size"]))
        if f["name"].strip() == "SCRIPT":
            err = check_script(f)
            if err:
                print("lstape: FAIL:", err)
                return 1
            ln = f["data"][4] | f["data"][5] << 8
            print("   script block OK, text %d bytes" % ln)
            if expect is not None and f["data"][8:8 + ln] != expect:
                print("lstape: FAIL: script text differs")
                return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
