#!/usr/bin/env python3
"""Dual-instance session driver for the two-layer
remote debugger (roadmap 26).

Runs two xmil instances (host + target) as separate
dualrun processes, each with XMIL_SERIAL pointing at
its own PTY slave, and cross-pumps the two PTY masters
in python - the null-modem cable between the machines.
Forwarding is frame-granular (store-and-forward), which
also makes the fault injections exact.

Modes:
  clean    plain forwarding; full session expected
  garbage  junk bytes injected before the first frame
           (the stub must resync and answer normally)
  flip     one payload byte of the first WRITE frame
           is corrupted (the stub must NAK, the host
           must retry and still complete the session)
  drop     after the PING reply, the cable is cut
           (the host must report link errors and keep
           answering its prompt - never hang)

usage: dualdrive.py MODE CORE SYSDIR HOSTD88 TARGETD88
       HOSTKEYS TARGETKEYS EVIDPREFIX TIMEOUT_S
Exit code 0 = the mode's assertions passed.
"""
import os
import pty
import select
import subprocess
import sys
import time

MODE, CORE, SYSDIR, HOSTD88, TARGETD88, HOSTKEYS, \
    TARGETKEYS, EVID, TIMEOUT = sys.argv[1:10]
TIMEOUT = float(TIMEOUT)


def scan_frames(buf, sync):
    """Split buf into (complete frames, rest). A frame
    is sync, cmd/status, len, payload[len], chk."""
    frames = []
    idx = 0
    while True:
        j = buf.find(bytes([sync]), idx)
        if j < 0:
            return frames, buf[idx:]
        if j + 4 > len(buf):
            return frames, buf[j:]
        ln = buf[j + 2]
        end = j + 4 + ln
        if end > len(buf):
            return frames, buf[j:]
        frames.append(buf[j:end])
        idx = end


def main():
    mh, sh = pty.openpty()
    mt, st = pty.openpty()
    slave_h = os.ttyname(sh)
    slave_t = os.ttyname(st)

    def spawn(disk, keys, ppm, d1, d2, d3, flag, slave):
        env = dict(os.environ)
        env["XMIL_SERIAL"] = slave
        args = ["./build/dualrun", CORE, disk, SYSDIR,
                "600000", ppm, "-", hex(d1), keys,
                "300", "10", "10", hex(d2), hex(d3), "-",
                hex(flag[0]), str(flag[1])]
        return subprocess.Popen(
            args, env=env, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)

    host = spawn(HOSTD88, HOSTKEYS, f"{EVID}-host.ppm",
                 0xE680, 0xE688, 0xE660, (0xE68C, 81), slave_h)
    target = spawn(TARGETD88, TARGETKEYS,
                   f"{EVID}-target.ppm",
                   0xE660, 0xE670, 0xE680, (0xE662, 255),
                   slave_t)

    os.set_blocking(mh, False)
    os.set_blocking(mt, False)
    h2t = open(f"{EVID}-h2t.bin", "wb")
    t2h = open(f"{EVID}-t2h.bin", "wb")

    st8 = {"garbage_done": False, "flip_done": False,
           "dropped": False, "naks": 0, "h_frames": 0,
           "t_frames": 0}
    hbuf = b""
    tbuf = b""

    def feed_h2t(data):
        nonlocal hbuf
        if MODE == "garbage" and not st8["garbage_done"]:
            data = b"\x00\xff\x13GARBAGE" + data
            st8["garbage_done"] = True
        hbuf += data
        frames, hbuf = scan_frames(hbuf, 0xA5)
        for raw in frames:
            st8["h_frames"] += 1
            if MODE == "flip" and raw[1] == 0x03 \
                    and not st8["flip_done"]:
                raw = bytearray(raw)
                raw[5] ^= 0xFF     # corrupt a payload byte
                raw = bytes(raw)
                st8["flip_done"] = True
            if st8["dropped"]:
                continue           # cable cut: byte is lost
            os.write(mt, raw)
            h2t.write(raw)
            h2t.flush()

    def feed_t2h(data):
        nonlocal tbuf
        tbuf += data
        frames, tbuf = scan_frames(tbuf, 0x5A)
        for raw in frames:
            st8["t_frames"] += 1
            if len(raw) >= 2 and raw[1] == 0x01:
                st8["naks"] += 1
            if MODE == "drop" and st8["t_frames"] == 1:
                # the PING reply passes, then the cable
                # is cut for everything after it
                os.write(mh, raw)
                t2h.write(raw)
                t2h.flush()
                st8["dropped"] = True
                continue
            if st8["dropped"]:
                continue
            os.write(mh, raw)
            t2h.write(raw)
            t2h.flush()

    deadline = time.time() + TIMEOUT
    while time.time() < deadline:
        if host.poll() is not None and \
                target.poll() is not None:
            break
        r, _, _ = select.select([mh, mt], [], [], 0.2)
        for fd in r:
            try:
                data = os.read(fd, 65536)
            except OSError:
                data = b""
            if not data:
                continue
            if fd == mh:
                feed_h2t(data)
            else:
                feed_t2h(data)
    for p in (host, target):
        if p.poll() is None:
            p.kill()
    hout = host.communicate()[0].decode("utf-8", "replace")
    tout = target.communicate()[0].decode("utf-8", "replace")
    open(f"{EVID}-host.log", "w").write(hout)
    open(f"{EVID}-target.log", "w").write(tout)
    print(f"host rc={host.returncode} target rc={target.returncode}")
    print(f"frames h2t={st8['h_frames']} t2h={st8['t_frames']} "
          f"naks={st8['naks']} flip={st8['flip_done']}")

    def ram(log, addr):
        tag = f"ram[0x{addr:04x}]="
        for line in log.splitlines():
            if line.startswith(tag):
                return [int(x, 16) for x in
                        line[len(tag):].split()]
        return None

    ok = True

    def check(name, cond):
        nonlocal ok
        print(f"{'PASS' if cond else 'FAIL'}: {name}")
        ok = ok and cond

    hev = ram(hout, 0xE680)
    hlink = ram(hout, 0xE688)
    tmir = ram(tout, 0xE660)
    tsig = ram(tout, 0xE670)
    if MODE in ("clean", "garbage", "flip"):
        check("host flag (quit completed)",
              "flag_reached=yes" in hout)
        check("target flag (stub exited)",
              "flag_reached=yes" in tout)
        check("host evidence RH", hev is not None
              and hev[0] == 0x52 and hev[1] == 0x48)
        check("stops == 4 (B,B,B,R)",
              hev is not None and hev[2] == 4)
        check("last reason R", hev is not None and hev[3] == 0x52)
        check("ping ok", hev is not None and hev[4] == 1)
        check("load ok", hev is not None and hev[5] == 1)
        check("maxpass readback 9",
              hev is not None and hev[6] == 9)
        check("demo count 9", hev is not None and hev[7] == 9)
        check("no link errors",
              hlink is not None and hlink[2] == 0)
        check("target mirror RD + gone", tmir is not None
              and tmir[0] == 0x52 and tmir[1] == 0x44
              and tmir[2] == 0xFF)
        check("target last reason R",
              tmir is not None and tmir[3] == 0x52)
        check("demo sig SM-OK + 9", tsig is not None
              and tsig[:5] == [0x53, 0x4D, 0x2D, 0x4F, 0x4B]
              and tsig[5] == 9)
        if MODE == "flip":
            check("flip injected", st8["flip_done"])
            check("stub NAKed the bad frame", st8["naks"] >= 1)
    elif MODE == "drop":
        check("host survived (evidence present)",
              hev is not None)
        check("ping had succeeded",
              hev is not None and hev[4] == 1)
        check("no stops after drop",
              hev is not None and hev[2] == 0)
        check("link error counted on quit path (>=1)",
              hlink is not None and hlink[2] >= 1)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
