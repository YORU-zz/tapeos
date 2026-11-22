#!/usr/bin/env python3
"""Convert a binary PPM (P6) to PNG using only the standard library."""
import sys, zlib, struct

def read_ppm(path):
    with open(path, 'rb') as f:
        data = f.read()
    # tokenize header (magic, width, height, maxval), allowing comments
    parts = []
    i = 0
    while len(parts) < 4:
        while i < len(data) and data[i:i+1].isspace():
            i += 1
        if data[i:i+1] == b'#':
            while i < len(data) and data[i:i+1] != b'\n':
                i += 1
            continue
        j = i
        while j < len(data) and not data[j:j+1].isspace():
            j += 1
        parts.append(data[i:j])
        i = j
    i += 1  # single whitespace after maxval
    assert parts[0] == b'P6', parts[0]
    w, h, mx = int(parts[1]), int(parts[2]), int(parts[3])
    assert mx == 255
    raw = data[i:i + w * h * 3]
    assert len(raw) == w * h * 3
    return w, h, raw

def chunk(typ, payload):
    c = struct.pack('>I', len(payload)) + typ + payload
    c += struct.pack('>I', zlib.crc32(typ + payload) & 0xffffffff)
    return c

def write_png(path, w, h, raw):
    stride = w * 3
    scan = bytearray()
    for y in range(h):
        scan.append(0)
        scan += raw[y * stride:(y + 1) * stride]
    png = b'\x89PNG\r\n\x1a\n'
    png += chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(bytes(scan), 6))
    png += chunk(b'IEND', b'')
    with open(path, 'wb') as f:
        f.write(png)

if __name__ == '__main__':
    w, h, raw = read_ppm(sys.argv[1])
    write_png(sys.argv[2], w, h, raw)
    print(f"{sys.argv[1]} -> {sys.argv[2]} ({w}x{h})")
