#!/usr/bin/env python3
"""smoke-check.py - minimal sanity checks on build outputs and
published images (used by CI; see docs/audit.md).

Checks:
  * the shell binary exists and fits the FD system-area cap
  * every images/*.d88 has a D88 header whose size field equals
    the actual file size
  * every images/*.wav starts with the RIFF/WAVE magic
  * the tape images are non-empty

Exit status: 0 = all checks pass, 1 = at least one failure.
"""
import glob
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)


shell = os.path.join(ROOT, "build", "tapeos-shell.bin")
check(os.path.isfile(shell), "build/tapeos-shell.bin missing (run ./build.sh)")
if os.path.isfile(shell):
    size = os.path.getsize(shell)
    check(0 < size <= 7936, "tapeos-shell.bin size out of range: %d" % size)

for path in sorted(glob.glob(os.path.join(ROOT, "images", "*.d88"))):
    data = open(path, "rb").read()
    rel = os.path.relpath(path, ROOT)
    check(len(data) >= 32, "%s: too small for a D88 header" % rel)
    if len(data) >= 32:
        declared = struct.unpack("<I", data[28:32])[0]
        check(declared == len(data),
              "%s: D88 header size %d != file size %d" % (rel, declared, len(data)))

for path in sorted(glob.glob(os.path.join(ROOT, "images", "*.wav"))):
    data = open(path, "rb").read(12)
    rel = os.path.relpath(path, ROOT)
    check(data[:4] == b"RIFF" and data[8:12] == b"WAVE",
          "%s: missing RIFF/WAVE magic" % rel)

for name in ("tapeos.cmt", "tsboot.cmt"):
    path = os.path.join(ROOT, "images", name)
    check(os.path.isfile(path) and os.path.getsize(path) > 0,
          "images/%s missing or empty" % name)

for msg in fails:
    print("FAIL", msg)
if fails:
    print("SMOKE CHECK FAILED")
    sys.exit(1)
print("SMOKE CHECK PASSED")
