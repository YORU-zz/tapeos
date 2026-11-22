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
#   tapeos.d88        bootable disk image of the M1 program
#   shell.d88         bootable disk image of the shell
#   mced.d88          bootable shell disk with the MCED program file
#   tapeos.cmt        tape image of the shell (IPL CMT boot)
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

echo "== disk / tape images =="
python3 tools/mkd88.py build/tapeos.bin build/tapeos.d88
python3 tools/mkd88.py build/tapeos-shell.bin build/shell.d88
python3 tools/mkd88.py build/tapeos-shell.bin build/mced.d88 0x2000 0x2000 full
python3 tools/putfile.py build/mced.d88 build/mced.bin MCED 0x4000
python3 tools/mkcmt.py build/tapeos-shell.bin build/tapeos.cmt

echo "== done =="
ls -l build/
