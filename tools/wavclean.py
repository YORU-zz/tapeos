#!/usr/bin/env python3
"""wavclean - host-side cleaner/converter for X1 TapeOS tapes.

The xmil core (io/cmt.c, tape-interop patch) plays and
records CLEAN PCM WAV only: it digitises with one fixed
threshold and decodes each bit cell by sampling the level
at a fixed point after the rising edge.  Everything a
real-world cassette rip needs beyond that - DC offset,
level drift, inverted polarity, noise - is handled here,
on the host, so the core can stay deterministic.

Pipeline for a dirty WAV:
  1. DC offset removal (midpoint of the 0.1%/99.9%
     percentile levels - the median would sit on the
     high rail, the leader being mostly bit-1 cells)
  2. normalisation (peak scaled to the canonical 12000)
  3. polarity detection: the tape is trial-decoded in
     both polarities (and, if needed, both speeds); the
     first decode whose info block checksum verifies
     wins, and the polarity is flipped if that is the
     inverted one
  4. hysteresis re-digitisation (Schmitt trigger at
     +/-20% of the canonical amplitude; the amplitude is
     estimated as the median absolute sample, which
     tracks the square wave's rails through level drift)
     and re-rendering in the canonical format

Canonical WAV (the interchange form, identical to what
the core itself records): RIFF/WAVE PCM, 48000 Hz,
16-bit signed mono, square wave high = +12000,
low = -12000, phase timings from the core's table:
  classic: bit 1 = high 380 us + low 280 us,
           bit 0 = high 100 us + low 280 us,
           decode sample point 185 us after the edge
  4800:    bit 1 = high 130 us + low  70 us,
           bit 0 = high  35 us + low 130 us,
           decode sample point  46 us after the edge
Phase sample counts use cumulative remainder carrying,
exactly as in the core's cmt_wav_savefile, so a .cmt
converted here and a tape recorded by the core to .wav
are bit-identical for the same bitstream.

usage:
  wavclean.py in.wav  out.wav [--4800]   clean a WAV
  wavclean.py in.cmt  out.wav [--4800]   .cmt -> canonical WAV
  wavclean.py in.wav  out.cmt [--4800]   cleaned WAV -> .cmt bits
  wavclean.py in.cmt  out.cmt            (refused: nothing to do)

The default speed is classic; --4800 selects the 4800 bps
timings for decoding/rendering.  If a WAV fails to
validate at the selected speed, the other speed is tried
automatically and the choice is reported.
"""
import struct
import sys
import wave

RATE = 48000
AMP = 12000
SPEEDS = {
    'classic': dict(hi1=380, hi0=100, lo1=280, lo0=280, sample=185),
    '4800': dict(hi1=130, hi0=35, lo1=70, lo0=130, sample=46),
}


# ---------- canonical rendering (shared spec with the core) ----------

def render_samples(bits, speed):
    """Bit list -> canonical sample list (ints, +/-AMP)."""
    t = SPEEDS[speed]
    out = []
    rem = 0
    for b in bits:
        for dur, lvl in ((t['hi1'] if b else t['hi0'], AMP),
                         (t['lo1'] if b else t['lo0'], -AMP)):
            cnt = (dur * RATE + rem) // 1000000
            rem = (dur * RATE + rem) % 1000000
            out.extend([lvl] * cnt)
    return out


def write_wav(path, samples):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(struct.pack('<%dh' % len(samples), *samples))


# ---------- WAV reading ----------

def read_wav(path):
    """-> (rate, [int samples in 16-bit domain, mono])"""
    with wave.open(path, 'rb') as w:
        nch = w.getnchannels()
        width = w.getsampwidth()
        rate = w.getframerate()
        raw = w.readframes(w.getnframes())
    if width == 1:
        vals = [(b - 128) * 256 for b in raw]
    elif width == 2:
        vals = list(struct.unpack('<%dh' % (len(raw) // 2), raw))
    elif width in (3, 4):
        n = len(raw) // width
        vals = [int.from_bytes(raw[i * width:(i + 1) * width],
                               'little', signed=True) >> (8 * width - 16)
                for i in range(n)]
    else:
        raise ValueError(f'unsupported sample width {width}')
    if nch > 1:
        vals = [sum(vals[i * nch:(i + 1) * nch]) // nch
                for i in range(len(vals) // nch)]
    return rate, vals


# ---------- decode (the core's rule, on host) ----------

def digitise_hysteresis(samples, hi_thr, lo_thr):
    """Schmitt trigger -> per-sample 0/1 levels."""
    out = []
    state = 0
    for s in samples:
        if s > hi_thr:
            state = 1
        elif s < lo_thr:
            state = 0
        out.append(state)
    return out


def decode_bits(levels, rate, speed):
    """Rising edge starts a cell; the bit is the level at
    the speed's sample point after the edge (as in the
    core's cmt_wav_decode and recorder)."""
    off = SPEEDS[speed]['sample'] * rate // 1000000
    bits = []
    prev = 0
    n = len(levels)
    for i, cur in enumerate(levels):
        if cur and not prev:
            j = i + off
            if j >= n:
                j = n - 1
            bits.append(levels[j])
        prev = cur
    return bits


def parse_tape(bits):
    """Validate the X1 logical format; -> [(info32, body)].
    Raises ValueError on any structural mismatch."""
    progs = []
    i = 0
    n = len(bits)

    def skip_run(i, val, minlen):
        j = i
        while j < n and bits[j] == val:
            j += 1
        if j - i < minlen:
            raise ValueError(f'run of {val} too short at bit {i}')
        return j

    def skip_exact(i, val, count):
        # The sync ones are a fixed count (41 / 21): they
        # merge into one run with the first byte's start
        # bit, so the run must NOT be consumed whole -
        # exactly `count` bits are sync, the next bit is
        # the start bit of the first framed byte.
        if i + count > n or any(bits[i + k] != val
                                for k in range(count)):
            raise ValueError(f'sync run of {val}x{count} '
                             f'missing at bit {i}')
        return i + count

    def read_byte(i):
        if i + 9 > n:
            raise ValueError('truncated byte')
        if bits[i] != 1:
            raise ValueError(f'start bit not 1 at bit {i}')
        v = 0
        for k in range(8):
            v = (v << 1) | bits[i + 1 + k]
        return v, i + 9

    while True:
        # leader: >=255 ones, ended by the first 0
        try:
            i = skip_run(i, 1, 255)
        except ValueError:
            break
        if i >= n or bits[i] != 0:
            break
        i = skip_run(i, 0, 1)          # sync zeros (40)
        i = skip_exact(i, 1, 41)       # sync ones (41)
        info = bytearray()
        for _ in range(34):
            b, i = read_byte(i)
            info.append(b)
        cks = info[32] | (info[33] << 8)
        if sum(info[:32]) & 0xFFFF != cks:
            raise ValueError('info block checksum mismatch')
        size = info[0x12] | (info[0x13] << 8)
        # data block: leader again, then `size` bytes
        i = skip_run(i, 1, 255)
        if i >= n or bits[i] != 0:
            raise ValueError('data leader not terminated')
        i = skip_run(i, 0, 1)          # sync zeros (20)
        i = skip_exact(i, 1, 21)       # sync ones (21)
        body = bytearray()
        for _ in range(size):
            b, i = read_byte(i)
            body.append(b)
        progs.append((bytes(info[:32]), bytes(body)))
    if not progs:
        raise ValueError('no valid program found')
    return progs


# ---------- .cmt ----------

def read_cmt(path):
    data = open(path, 'rb').read()
    bits = []
    for byte in data:
        for k in range(8):
            bits.append((byte >> (7 - k)) & 1)
    return bits


def write_cmt(path, bits):
    bits = list(bits)
    while len(bits) % 8:
        bits.append(1)
    out = bytearray()
    for i in range(0, len(bits), 8):
        v = 0
        for bit in bits[i:i + 8]:
            v = (v << 1) | bit
        out.append(v)
    with open(path, 'wb') as f:
        f.write(out)


# ---------- cleaning ----------

def clean_wav(path, want_speed):
    """Dirty WAV -> (bits, report dict).  Tries the wanted
    speed first, both polarities, then the other speed."""
    rate, samples = read_wav(path)
    if not samples:
        raise ValueError('empty WAV')
    # DC offset: the midpoint between the 0.1% and 99.9%
    # percentile levels.  (The median is useless here: the
    # leader's bit-1 cells are high for 380 of every 660 us,
    # so more than half of all samples sit on the high rail.)
    srt = sorted(samples)
    lo = srt[len(srt) // 1000]
    hi = srt[len(srt) - 1 - len(srt) // 1000]
    dc = (hi + lo) // 2
    centred = [s - dc for s in samples]
    # Amplitude: the median absolute centred sample.  A
    # square wave sits on its rails almost all the time,
    # so this tracks the true amplitude through level
    # drift, where a peak-based estimate (inflated by
    # noise spikes and the loudest passage) would leave
    # quiet passages below the hysteresis thresholds.
    absrt = sorted(abs(s) for s in centred)
    peak = absrt[len(absrt) // 2]
    if peak == 0:
        raise ValueError('silent WAV')
    scale = AMP / peak
    norm = [int(round(s * scale)) for s in centred]
    hi_thr = AMP // 5
    lo_thr = -AMP // 5
    speeds = [want_speed, '4800' if want_speed == 'classic' else 'classic']
    for speed in speeds:
        for inverted in (False, True):
            src = [-s for s in norm] if inverted else norm
            levels = digitise_hysteresis(src, hi_thr, lo_thr)
            bits = decode_bits(levels, rate, speed)
            try:
                progs = parse_tape(bits)
            except ValueError:
                continue
            return bits, dict(dc=dc, peak=peak, speed=speed,
                              inverted=inverted, progs=progs,
                              rate=rate)
    raise ValueError('no polarity/speed combination validates '
                     '(not a readable X1 tape?)')


def main(argv):
    args = [a for a in argv[1:] if not a.startswith('--')]
    speed = '4800' if '--4800' in argv else 'classic'
    if len(args) != 2:
        print(__doc__)
        return 2
    inp, outp = args
    in_wav = inp.lower().endswith('.wav')
    in_cmt = inp.lower().endswith('.cmt')
    out_wav = outp.lower().endswith('.wav')
    out_cmt = outp.lower().endswith('.cmt')
    if not (in_wav or in_cmt) or not (out_wav or out_cmt):
        print('error: input/output must be .wav or .cmt', file=sys.stderr)
        return 2
    if in_cmt and out_cmt:
        print('error: .cmt -> .cmt has nothing to do', file=sys.stderr)
        return 2

    if in_cmt:
        bits = read_cmt(inp)
        try:
            progs = parse_tape(bits)
            names = ', '.join(
                p[0][1:14].decode('ascii', 'replace').strip()
                for p in progs)
        except ValueError:
            names = '(unvalidated bitstream)'
            progs = []
        print(f'{inp}: .cmt, {len(bits)} bits, '
              f'{len(progs)} program(s): {names}')
        used_speed = speed
    else:
        bits, rep = clean_wav(inp, speed)
        names = ', '.join(
            p[0][1:14].decode('ascii', 'replace').strip()
            for p in rep['progs'])
        print(f"{inp}: wav {rep['rate']} Hz, dc offset {rep['dc']}, "
              f"peak {rep['peak']}, polarity "
              f"{'INVERTED (corrected)' if rep['inverted'] else 'normal'}, "
              f"speed {rep['speed']}, {len(bits)} bits, "
              f"{len(rep['progs'])} program(s): {names}")
        used_speed = rep['speed']

    if out_wav:
        write_wav(outp, render_samples(bits, used_speed))
        print(f'{outp}: canonical WAV written '
              f'(48000 Hz 16-bit mono, {used_speed} timings)')
    else:
        write_cmt(outp, bits)
        print(f'{outp}: .cmt written ({len(bits)} bits)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
