#!/usr/bin/env python3
"""Convert the Misaki font (BDF) into TapeOS's on-disk font file.

Source font: Misaki Gothic, from the official distribution
  https://littlelimit.net/misaki.htm
  archive: misaki_bdf_2021-05-05.zip -> misaki_gothic.bdf
  (kept in ~/workspace/ourobo-ros/downloads/misaki_bdf/)
  Copyright (C) 2002-2021 Num Kadoma. The Misaki fonts are free
  software: unlimited permission to use, copy and distribute,
  with or without modification, commercially or not, provided
  as-is without warranty (see downloads/misaki_bdf/misaki.txt).

Output format (default build/misaki8.fnt), all integers LE:
  +0   magic "MSK8"
  +4   format version (1)
  +5   glyph cell size in pixels (8)
  +6   grid rows (84) / +7 grid cols (94)   -- JIS X 0208 grid
  +8   glyph bytes (8) / +9 header bytes (16)
  +10  reserved (6 bytes, zero)
  +16  glyph bitmaps: index = (ku - 1) * 94 + (ten - 1),
       8 bytes per glyph, row 0 = top row, MSB = leftmost pixel.
       JIS slots the font does not define are all zero.
Total size: 16 + 84 * 94 * 8 = 63,184 bytes. Only ku 1..84 are
stored: JIS X 0208 assigns no characters in ku 85..94 (the
Misaki font defines none there either), and 63,184 bytes keeps
the file inside the TOSD directory's 16-bit size field.

Glyph extraction: each BDF char (Unicode ENCODING) that maps to
JIS X 0208 via EUC-JP (two bytes A1..FE) is composited into the
8x8 cell from its trimmed BITMAP using BBX: the bitmap's top row
lands on cell row (FONT_ASCENT - yoff - height), its left column
on cell column xoff. Bits are MSB-first.

usage: mkfont.py <misaki_gothic.bdf> [out.fnt]
"""
import re
import sys

KU_MAX = 84
TEN_MAX = 94
HEADER_SIZE = 16
GLYPH_SIZE = 8


def parse_bdf(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.search(r"FONT_ASCENT (\d+)", text)
    ascent = int(m.group(1)) if m else 6
    glyphs = {}
    pat = re.compile(
        r"STARTCHAR \S+\nENCODING (\d+)\nSWIDTH [^\n]*\nDWIDTH \d+ 0\n"
        r"BBX (-?\d+) (-?\d+) (-?\d+) (-?\d+)\nBITMAP\n([0-9A-Fa-f\n]*?)ENDCHAR")
    for m in pat.finditer(text):
        enc, w, h, xo, yo, bmp = m.groups()
        rows = bmp.strip().split("\n") if bmp.strip() else []
        glyphs[int(enc)] = (int(w), int(h), int(xo), int(yo), rows)
    return ascent, glyphs


def composite(ascent, glyph):
    """Place a trimmed BDF bitmap into an 8x8 cell (8 row bytes)."""
    w, h, xo, yo, rows = glyph
    cell = [0] * 8
    top = ascent - yo - h
    for i, hexrow in enumerate(rows):
        r = top + i
        if not 0 <= r < 8:
            continue
        nbytes = (w + 7) // 8
        bits = int(hexrow, 16) >> (nbytes * 8 - w) if w else 0
        for j in range(w):
            if bits & (1 << (w - 1 - j)):
                c = xo + j
                if 0 <= c < 8:
                    cell[r] |= 0x80 >> c
    return bytes(cell)


def jis_code(ch):
    """Unicode char -> (ku, ten) in JIS X 0208, or None."""
    try:
        b = ch.encode("euc_jp")
    except (UnicodeEncodeError, LookupError):
        return None
    if len(b) == 2 and 0xA1 <= b[0] <= 0xFE and 0xA1 <= b[1] <= 0xFE:
        return b[0] - 0xA0, b[1] - 0xA0
    return None


def art(cell):
    return "\n".join(
        "".join("#" if row & (0x80 >> c) else "." for c in range(8))
        for row in cell)


def main():
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else "build/misaki8.fnt"
    ascent, glyphs = parse_bdf(src)
    print(f"{src}: {len(glyphs)} BDF glyphs, FONT_ASCENT={ascent}")

    data = bytearray(HEADER_SIZE + KU_MAX * TEN_MAX * GLYPH_SIZE)
    data[0:4] = b"MSK8"
    data[4] = 1
    data[5] = 8
    data[6] = KU_MAX
    data[7] = TEN_MAX
    data[8] = GLYPH_SIZE
    data[9] = HEADER_SIZE

    filled = 0
    clipped = []
    samples = {}
    for cp, glyph in glyphs.items():
        code = jis_code(chr(cp))
        if code is None:
            continue
        ku, ten = code
        assert ku <= KU_MAX, f"glyph outside stored grid: {code}"
        w, h, xo, yo, _ = glyph
        top = ascent - yo - h
        if xo < 0 or xo + w > 8 or top < 0 or top + h > 8:
            clipped.append((ku, ten, chr(cp)))
        cell = composite(ascent, glyph)
        idx = (ku - 1) * TEN_MAX + (ten - 1)
        off = HEADER_SIZE + idx * GLYPH_SIZE
        data[off:off + GLYPH_SIZE] = cell
        filled += 1
        if chr(cp) in "王将あ世":
            samples[chr(cp)] = cell
    print(f"JIS X 0208 glyphs placed: {filled} "
          f"(grid {KU_MAX * TEN_MAX} slots), clipped: {len(clipped)}")
    if clipped:
        print("  clipped glyphs (ku,ten,char):", clipped[:8])

    with open(out, "wb") as f:
        f.write(data)
    print(f"{out}: {len(data)} bytes")

    # self-check: the demo glyphs must be non-blank
    for ch in "王将":
        assert any(samples[ch]), f"glyph for {ch} is blank!"
    print("sample glyphs from the converted file:")
    for ch in "王将あ":
        print(f"  {ch} (ku,ten)={jis_code(ch)}")
        print("\n".join("    " + ln for ln in art(samples[ch]).split("\n")))


if __name__ == "__main__":
    main()
