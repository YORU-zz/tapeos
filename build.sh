#!/bin/sh
# TapeOS (Sharp X1) build — provisional publication tree
#
# Assembles the X1 targets with pasmo and builds the disk/tape
# images. Requires: pasmo (Z80 cross assembler) and python3.
# Override the assembler location with e.g.  PASMO=/path/to/pasmo ./build.sh
#
# Outputs (in build/):
#   tapeos.bin        M1 minimal boot program
#   tapeos-shell.bin  TapeOS monitor shell (the OS body)
#   tapeos-4800.bin   shell variant for 4800 bps tape access
#   edas.bin          EDAS editor/assembler (TOSD program, load 0x4000)
#   edasr.bin         EDAS, TOSR relocatable edition
#   newasm.bin        NEWASM on-machine assembler (TOSD program)
#   sced.bin          SCED screen editor (TOSD program)
#   mced.bin          MCED MicroEmacs-style editor (TOSD program)
#   mdb.bin           MDB Markdown browser (TOSD program)
#   tbc.bin           TBC TinyBASIC compiler (TOSD program)
#   tbi.bin           TBI TinyBASIC interpreter (JIT, TOSD program)
#   scc.bin           SCC Small-C compiler (TOSD program)
#   sci.bin           SCI Small-C interpreter (JIT, TOSD program)
#   disasm.bin        DISASM Z80 disassembler (TOSD program)
#   rdbg.tosr         RDBG standalone debugger (TOSR program,
#                     built by tools/mkrdbg.py)
#   edis.bin          EDIS disassembler module for EDAS (load 0x9000)
#   tsload.bin        Tiny Scheme stage-1 tape loader (IPL loadable)
#   scmcore.bin       SCMCORE embedded Tiny Scheme core (TOSD file,
#                     loaded by TinyBASIC/SmallC programs at start)
#   tsdemo.bin        TSDEMO demo program for the Tiny Scheme tape
#   kanji.bin         KANJI kanji display program (TOSD, load 0x4000;
#                     the shell's K command, demoted in roadmap 26)
#   help.bin          HELP full help text program (TOSD, load 0x4000)
#   ep56test.bin      EP56TEST IOCS body checks (TOSD, load 0x4000)
#   rstub.bin         RSTUB remote-debug target stub (TOSD, 0x4000)
#   rhdemo.bin        RHDEMO self-modifying debug demo (load 0x8000)
#   rhio.bin          RHIO host serial frame layer (load 0xD000)
#   rhfmt.bin         RHFMT hex-dump renderer (cursor seat in shell)
#   tapeos.d88        bootable disk image of the M1 program
#   shell.d88         bootable disk image of the shell
#   mced.d88          bootable shell disk with the MCED program file
#   mdb.d88           bootable shell disk with MDB, SCED and sample docs
#   tinybasic.d88     bootable shell disk with TBC/TBI and samples
#   smallc.d88        bootable shell disk with SCC/SCI and samples
#   disasm.d88        bootable shell disk with the DISASM program file
#   rdbg.d88          bootable shell disk with the RDBG program file
#   edas.d88          bootable shell disk with EDAS and the EDIS module
#   ep56.d88          bootable shell disk with the two-layer remote
#                     debugger kit (RSTUB, RHIO, RHFMT, RHDEMO,
#                     RHDBG.TB, TBI, SCMCORE, KANJI, HELP, EP56TEST)
#   tapeos.cmt        tape image of the shell (IPL CMT boot)
#   tsboot.cmt        Tiny Scheme 2-stage boot tape (loader +
#                     script block + TapeOS + TSDEMO)
set -e
cd "$(dirname "$0")"

PASMO="${PASMO:-pasmo}"

mkdir -p build

echo "== assemble =="
"$PASMO" src/boot.z80 build/tapeos.bin build/tapeos.lst
"$PASMO" src/tapeos.z80 build/tapeos-shell.bin build/tapeos-shell.lst
"$PASMO" src/tapeos_4800.z80 build/tapeos-4800.bin build/tapeos-4800.lst
"$PASMO" src/edas.z80 build/edas.bin build/edas.lst
"$PASMO" src/edasr.z80 build/edasr.bin build/edasr.lst
"$PASMO" src/newasm.z80 build/newasm.bin build/newasm.lst
"$PASMO" src/sced.z80 build/sced.bin build/sced.lst
"$PASMO" src/mced.z80 build/mced.bin build/mced.lst
"$PASMO" src/mdb.z80 build/mdb.bin build/mdb.lst
python3 tools/mklibtab.py src/tblib.z80 "$PASMO" build
python3 tools/genstub.py tools/stub-template.bin build/stub.inc
"$PASMO" -I src -I build src/tbc.z80 build/tbc.bin build/tbc.lst
"$PASMO" -I src -I build src/tbi.z80 build/tbi.bin build/tbi.lst
python3 tools/mkclibtab.py src/clib.z80 "$PASMO" build
"$PASMO" -I src -I build src/scc.z80 build/scc.bin build/scc.lst
"$PASMO" -I src -I build src/sci.z80 build/sci.bin build/sci.lst
"$PASMO" src/disasm.z80 build/disasm.bin build/disasm.lst
"$PASMO" src/edis.z80 build/edis.bin build/edis.lst
PASMO="$PASMO" python3 tools/mkrdbg.py
# Roadmap 26: demoted shell programs + the two-layer debugger.
"$PASMO" -I src src/kanji.z80 build/kanji.bin build/kanji.lst
"$PASMO" -I src src/help.z80 build/help.bin build/help.lst
"$PASMO" -I src src/ep56test.z80 build/ep56test.bin build/ep56test.lst
"$PASMO" -I src src/rstub.z80 build/rstub.bin build/rstub.lst
"$PASMO" -I src src/rhdemo.z80 build/rhdemo.bin build/rhdemo.lst
# RHIO's frame buffer is the shell's resident Scheme seat and
# RHFMT lives in its cursor seat; both addresses come from the
# shell listing.
python3 - <<'PYEOF'
import re
def equ(path, name):
    for line in open(path):
        m = re.match(rf"^{name}\s+EQU\s+0([0-9A-F]+)H", line)
        if m:
            return int(m.group(1), 16)
    raise SystemExit(f"label {name} not found in {path}")
seat = equ("build/tapeos-shell.lst", "scheme_seat")
hook = equ("build/tapeos-shell.lst", "cursor_hook")
with open("build/bufaddr.inc", "w") as f:
    f.write("; generated by build.sh - the shell's resident\n"
            "; Scheme seat, RHIO's frame buffer\n")
    f.write("BUF             equ     0%04xh\n" % seat)
with open("build/fmtorg.inc", "w") as f:
    f.write("; generated by build.sh - RHFMT lives in the\n"
            "; shell's cursor seat\n")
    f.write("FMTORG          equ     0%04xh\n" % (hook + 1))
    f.write("BUF             equ     0%04xh\n" % seat)
with open("build/fmtorg.txt", "w") as f:
    f.write("0x%04X\n" % (hook + 1))
PYEOF
"$PASMO" -I src -I build src/rhio.z80 build/rhio.bin build/rhio.lst
"$PASMO" -I src -I build src/rhfmt.z80 build/rhfmt.bin build/rhfmt.lst
# Tiny Scheme: the final-load chunk is embedded into the loader
"$PASMO" -I src src/ldfin.z80 build/ldfin.bin
python3 - <<'PYEOF'
data = open("build/ldfin.bin", "rb").read()
with open("build/fin.inc", "w") as f:
    f.write("; generated by build.sh from src/ldfin.z80\n")
    for i in range(0, len(data), 8):
        f.write("        db      " + ", ".join(
            "0%02xh" % b for b in data[i:i + 8]) + "\n")
with open("build/finlen.inc", "w") as f:
    f.write("FIN_LEN equ %d\n" % len(data))
PYEOF
"$PASMO" -I src -I build src/tsload.z80 build/tsload.bin build/tsload.lst
"$PASMO" src/tsdemo.z80 build/tsdemo.bin
# SCMCORE: the embedded Tiny Scheme core (roadmap 61).  The
# core source (src/tscore.inc) is shared with TSLOAD; the
# generator emits the reduced embedded variant.
python3 tools/mkscmcore.py src/tscore.inc build/scmcore-gen.inc
"$PASMO" -I src -I build src/scmcore.z80 build/scmcore.bin build/scmcore.lst

echo "== disk / tape images =="
python3 tools/mkd88.py build/tapeos.bin build/tapeos.d88
python3 tools/mkd88.py build/tapeos-shell.bin build/shell.d88
python3 tools/mkd88.py build/tapeos-shell.bin build/mced.d88 0x2000 0x2000 full
python3 tools/putfile.py build/mced.d88 build/mced.bin MCED 0x4000
python3 tools/mkd88.py build/tapeos-shell.bin build/mdb.d88 0x2000 0x2000 full
python3 tools/putfiles.py build/mdb.d88 \
    build/mdb.bin MDB 0x4000 \
    build/sced.bin SCED 0x4000 \
    src/INDEX.MD INDEX.MD 0 \
    src/GUIDE.MD GUIDE.MD 0 \
    src/REF.MD REF.MD 0 \
    src/JIS.MD JIS.MD 0
python3 tools/mkd88.py build/tapeos-shell.bin build/tinybasic.d88 0x2000 0x2000 full
python3 tools/putfiles.py build/tinybasic.d88 \
    build/tbc.bin TBC 0x4000 \
    build/tbi.bin TBI 0x4000 \
    build/scmcore.bin SCMCORE 0x9000 \
    src/fib.tb FIB.TB 0x8000 \
    src/calc.tb CALC.TB 0x8000 \
    src/game.tb GAME.TB 0x8000 \
    src/scm.tb SCM.TB 0x8000
python3 tools/mkd88.py build/tapeos-shell.bin build/smallc.d88 0x2000 0x2000 full
python3 tools/putfiles.py build/smallc.d88 \
    build/scc.bin SCC 0x4000 \
    build/sci.bin SCI 0x4000 \
    build/scmcore.bin SCMCORE 0x9000 \
    src/fib.c FIB.C 0x8000 \
    src/calc.c CALC.C 0x8000 \
    src/game.c GAME.C 0x8000 \
    src/scm.c SCM.C 0x8000
python3 tools/mkd88.py build/tapeos-shell.bin build/disasm.d88 0x2000 0x2000 full
python3 tools/putfile.py build/disasm.d88 build/disasm.bin DISASM 0x4000
python3 tools/mkd88.py build/tapeos-shell.bin build/rdbg.d88 0x2000 0x2000 full
python3 tools/putfile.py build/rdbg.d88 build/rdbg.tosr RDBG 0x4000
python3 tools/mkd88.py build/tapeos-shell.bin build/edas.d88 0x2000 0x2000 full
python3 tools/putfiles.py build/edas.d88 \
    build/edas.bin EDAS 0x4000 \
    build/edis.bin EDIS 0x9000
python3 tools/mkd88.py build/tapeos-shell.bin build/ep56.d88 0x2000 0x2000 full
python3 tools/putfiles.py build/ep56.d88 \
    build/rstub.bin RSTUB 0x4000 \
    build/tbi.bin TBI 0x4000 \
    build/scmcore.bin SCMCORE 0x9000 \
    build/rhio.bin RHIO 0xD000 \
    build/rhfmt.bin RHFMT "$(cat build/fmtorg.txt)" \
    build/rhdemo.bin RHDEMO 0x8F00 \
    src/rhdbg.tb RHDBG.TB 0x8000 \
    build/kanji.bin KANJI 0x4000 \
    build/help.bin HELP 0x4000 \
    build/ep56test.bin EP56TEST 0x4000
python3 tools/mkcmt.py build/tapeos-shell.bin build/tapeos.cmt
python3 tools/mktape.py --loader build/tsload.bin \
    --script src/stage2.scm --out build/tsboot.cmt \
    --file "TAPEOS:Bin:2000:build/tapeos-shell.bin" \
    --file "TSDEMO:Bin:8000:build/tsdemo.bin"

echo "== done =="
ls -l build/
