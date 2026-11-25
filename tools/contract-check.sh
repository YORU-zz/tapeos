#!/bin/sh
# contract-check.sh - fast contract verification for the TapeOS
# interface specification (see ../docs/interface-spec.md).
#
# Repository edition: this tree publishes the X1 build only, so the
# bundled manifest (tools/contract-manifest.txt) carries the X1
# subset of the development tree's full manifest, with paths
# remapped to this repository's layout. Every expected value is
# identical to the full manifest's corresponding entry.
#
# Run ./build.sh first: the HASH / IOCS / SIZE checks read the
# build outputs under build/.
#
# Verifies, in well under a few minutes and without running any
# emulator, the machine-checkable parts of the interface contract:
#
#   HASH  sha256 byte-identity of non-monitor deliverable binaries
#         (the monitor/shell binaries themselves are covered by the
#         IOCS checks instead, so monitor features may grow while
#         the interface stays fixed)
#   IOCS  byte-identity of the IOCS jump-table template inside
#         each canonical monitor build (the base table is
#         28 entries x JP = 84 bytes; the X1 builds carry
#         31 entries = 93 bytes, recorded as a 5th field per line)
#   EQU   memory-map / interface constants in the machine incs and
#         common.inc keep their contract values
#   TEXT  a literal that must remain in a source file (e.g. the
#         0x4000 program origin of the on-machine tools)
#   SIZE  component size caps from the specification
#
# The expected values live in contract-manifest.txt next to this
# script.  When a legitimate interface change lands (with the spec
# updated and the affected suites re-run), refresh the HASH/IOCS
# hashes with:
#
#   sh contract-check.sh --generate
#
# EQU / TEXT / SIZE lines are the contract itself: edit them only
# together with interface-spec.md.
#
# Exit status: 0 = all checks PASS, 1 = at least one FAIL.

set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$PWD
MANIFEST="tools/contract-manifest.txt"

if [ "${1:-}" = "--generate" ]; then
    python3 - "$MANIFEST" <<'PYEOF'
import hashlib, re, sys
man = sys.argv[1]
out = []
for line in open(man, encoding="utf-8"):
    line = line.rstrip("\n")
    parts = line.split()
    if len(parts) >= 3 and parts[0] == "HASH":
        path = parts[2]
        try:
            h = hashlib.sha256(open(path, "rb").read()).hexdigest()
            out.append("HASH %s %s" % (h, path))
        except OSError:
            out.append(line)
    elif len(parts) >= 4 and parts[0] == "IOCS":
        path, off = parts[1], int(parts[2], 0)
        blen = int(parts[4], 0) if len(parts) >= 5 else 84
        try:
            blk = open(path, "rb").read()[off:off + blen]
            h = hashlib.sha256(blk).hexdigest()
            if len(parts) >= 5:
                out.append("IOCS %s %s %s %s" % (path, parts[2], h, parts[4]))
            else:
                out.append("IOCS %s %s %s" % (path, parts[2], h))
        except OSError:
            out.append(line)
    else:
        out.append(line)
open(man, "w", encoding="utf-8").write("\n".join(out) + "\n")
print("manifest hashes refreshed:", man)
PYEOF
    exit $?
fi

[ -f "$MANIFEST" ] || { echo "FAIL: manifest not found: $MANIFEST"; exit 1; }

FAIL=0
N_HASH=0; N_IOCS=0; N_EQU=0; N_TEXT=0; N_SIZE=0

# --- HASH / SIZE: one pass over the manifest with shell tools ---
while read -r kind a b c; do
    case "$kind" in
    HASH)
        N_HASH=$((N_HASH + 1))
        if [ ! -f "$b" ]; then
            echo "FAIL HASH missing file: $b"; FAIL=1
        else
            GOT=$(sha256sum "$b" | awk '{print $1}')
            if [ "$GOT" != "$a" ]; then
                echo "FAIL HASH $b (expected $a, got $GOT)"; FAIL=1
            fi
        fi
        ;;
    SIZE)
        N_SIZE=$((N_SIZE + 1))
        if [ ! -f "$a" ]; then
            echo "FAIL SIZE missing file: $a"; FAIL=1
        else
            SZ=$(stat -c %s "$a")
            if [ "$SZ" -gt "$b" ]; then
                echo "FAIL SIZE $a is $SZ bytes, cap is $b"; FAIL=1
            fi
        fi
        ;;
    esac
done < "$MANIFEST"

# --- IOCS / EQU: python for byte slicing and equate parsing ---
PYOUT=$(python3 - "$MANIFEST" <<'PYEOF'
import hashlib, re, sys
fails = []
n_iocs = 0
n_equ = 0
def equ_value(path, name):
    try:
        src = open(path, errors="replace").read()
    except OSError:
        return None
    m = re.search(r'(?m)^\s*' + re.escape(name) + r'\s+equ\s+([0-9][0-9a-fA-F]*h|\d+)\b', src)
    if not m:
        return None
    v = m.group(1)
    return int(v[:-1], 16) if v.lower().endswith("h") else int(v)
for line in open(sys.argv[1], encoding="utf-8"):
    parts = line.split()
    if not parts:
        continue
    if parts[0] == "IOCS" and len(parts) >= 4:
        n_iocs += 1
        path, off, want = parts[1], int(parts[2], 0), parts[3]
        blen = int(parts[4], 0) if len(parts) >= 5 else 84
        try:
            blk = open(path, "rb").read()[off:off + blen]
        except OSError:
            fails.append("FAIL IOCS missing file: %s" % path)
            continue
        if len(blk) != blen or any(blk[i] != 0xC3 for i in range(0, blen, 3)):
            fails.append("FAIL IOCS %s: template at %s is not %d JP entries" % (path, parts[2], blen // 3))
            continue
        got = hashlib.sha256(blk).hexdigest()
        if got != want:
            fails.append("FAIL IOCS %s template hash (expected %s, got %s)" % (path, want, got))
    elif parts[0] == "EQU" and len(parts) >= 4:
        n_equ += 1
        path, name, want = parts[1], parts[2], int(parts[3], 0)
        got = equ_value(path, name)
        if got is None:
            fails.append("FAIL EQU %s %s not found" % (path, name))
        elif got != want:
            fails.append("FAIL EQU %s %s = %s, contract is %s" % (path, name, got, want))
for f in fails:
    print(f)
print("COUNTS %d %d" % (n_iocs, n_equ))
print("PYFAIL %d" % (1 if fails else 0))
PYEOF
)
echo "$PYOUT" | grep '^FAIL' || true
COUNTS=$(echo "$PYOUT" | grep '^COUNTS')
N_IOCS=$(echo "$COUNTS" | awk '{print $2}')
N_EQU=$(echo "$COUNTS" | awk '{print $3}')
echo "$PYOUT" | grep -q '^PYFAIL 1$' && FAIL=1

# --- TEXT: literal presence in sources ---
while read -r kind file lit; do
    if [ "$kind" = "TEXT" ]; then
        N_TEXT=$((N_TEXT + 1))
        if ! grep -qF "$lit" "$file" 2>/dev/null; then
            echo "FAIL TEXT $file does not contain: $lit"; FAIL=1
        fi
    fi
done < "$MANIFEST"

echo "contract-check: HASH x$N_HASH, IOCS x$N_IOCS, EQU x$N_EQU, TEXT x$N_TEXT, SIZE x$N_SIZE (root: $ROOT)"
if [ "$FAIL" = 0 ]; then
    echo "CONTRACT CHECK PASSED"
else
    echo "CONTRACT CHECK FAILED"
fi
exit $FAIL
