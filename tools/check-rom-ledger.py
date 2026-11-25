#!/usr/bin/env python3
"""check-rom-ledger.py - allowlist check for binary files.

Every binary file in this repository tree must be listed in
tools/rom-ledger.tsv (the provenance ledger; see
docs/rom-provenance.md). A binary that is not listed, or whose
sha256 differs from the ledger, fails the check. This is an
allowlist: it exists so that foreign binaries (hardware ROM dumps,
third-party program bodies, ...) can never slip into the published
tree unnoticed.

Entries whose path ends with "/" are directory entries: any binary
under that directory is allowed without an individual hash (used
for build/, whose contents are generated from this repository's own
sources by build.sh; representative outputs are pinned by the
contract check instead).

Exit status: 0 = all binaries accounted for, 1 = violation.
"""
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LEDGER = os.path.join(ROOT, "tools", "rom-ledger.tsv")


def is_binary(data):
    return b"\x00" in data


def main():
    exact = {}
    dirs = []
    with open(LEDGER, encoding="utf-8") as f:
        header = f.readline()
        if not header.startswith("path\t"):
            print("FAIL: ledger header missing")
            return 1
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            parts = line.split("\t")
            path, sha = parts[0], parts[1]
            if path.endswith("/"):
                dirs.append(path)
            else:
                exact[path] = sha

    fails = []
    n_bin = 0
    for dirpath, dirnames, filenames in os.walk(ROOT):
        if ".git" in dirnames:
            dirnames.remove(".git")
        for name in filenames:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, ROOT).replace(os.sep, "/")
            with open(full, "rb") as fh:
                data = fh.read()
            if not is_binary(data):
                continue
            n_bin += 1
            if rel in exact:
                want = exact[rel]
                if want != "-":
                    got = hashlib.sha256(data).hexdigest()
                    if got != want:
                        fails.append(
                            "FAIL sha256 mismatch: %s (ledger %s, actual %s)"
                            % (rel, want, got))
            elif any(rel.startswith(d) for d in dirs):
                pass
            else:
                fails.append("FAIL binary not in ledger: %s" % rel)

    for path in exact:
        if not os.path.isfile(os.path.join(ROOT, path)):
            fails.append("FAIL ledger entry missing on disk: %s" % path)

    for msg in fails:
        print(msg)
    print("rom-ledger check: %d binary files, %d exact entries, %d directory entries"
          % (n_bin, len(exact), len(dirs)))
    if fails:
        print("ROM LEDGER CHECK FAILED")
        return 1
    print("ROM LEDGER CHECK PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
