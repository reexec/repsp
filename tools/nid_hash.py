#!/usr/bin/env python3
"""Compute PSP firmware NIDs (first 4 bytes LE of SHA-1) and match NID lists against candidate names.

Python standard library only. Usage:
  python -I tools/nid_hash.py sceKernelSetCompiledSdkVersion sceUtilityLoadAvModule
  python -I tools/nid_hash.py --match nids.txt --candidates names.txt
"""

from __future__ import annotations

import argparse
import hashlib
import re
import struct
import sys
from pathlib import Path

HEX_NID_RE = re.compile(r"0x([0-9a-fA-F]{8})")


def compute_nid(name: str) -> int:
    digest = hashlib.sha1(name.encode("ascii")).digest()
    return struct.unpack("<I", digest[:4])[0]


def load_target_nids(path: Path) -> list[tuple[str, int]]:
    targets: list[tuple[str, int]] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        m = HEX_NID_RE.search(line)
        if not m:
            continue
        nid = int(m.group(1), 16)
        label = line[: m.start()].strip().rstrip(":,")
        targets.append((label, nid))
    return targets


def load_candidates(paths: list[str]) -> list[str]:
    names: list[str] = []
    seen: set[str] = set()
    for p in paths:
        if p == "-":
            lines = sys.stdin.read().splitlines()
        else:
            lines = Path(p).read_text(encoding="utf-8").splitlines()
        for raw in lines:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            token = line.split()[0]
            if token not in seen:
                seen.add(token)
                names.append(token)
    return names


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Compute PSP NIDs from SDK names or match NID lists against candidate names."
    )
    parser.add_argument("names", nargs="*", help="Function names to hash")
    parser.add_argument(
        "--match",
        type=Path,
        help="File containing hex NIDs (0xXXXXXXXX) to match against candidates",
    )
    parser.add_argument(
        "--candidates",
        action="append",
        default=[],
        help="File containing candidate function names (one per line, or '-' for stdin)",
    )
    args = parser.parse_args(argv)

    if args.match:
        targets = load_target_nids(args.match)
        candidates = list(args.names)
        if args.candidates:
            candidates.extend(load_candidates(args.candidates))
        nid_map: dict[int, list[str]] = {}
        for name in candidates:
            nid = compute_nid(name)
            nid_map.setdefault(nid, []).append(name)

        matched = 0
        unmatched: list[tuple[str, int]] = []
        for label, nid in targets:
            hits = nid_map.get(nid)
            prefix = f"{label}:" if label else ""
            if hits:
                matched += 1
                print(f"MATCH   {prefix}0x{nid:08X} -> {', '.join(hits)}")
            else:
                unmatched.append((label, nid))
        print(f"\nMatched {matched}/{len(targets)} NIDs; {len(unmatched)} unmatched.")
        for label, nid in unmatched:
            prefix = f"{label}:" if label else ""
            print(f"UNKNOWN {prefix}0x{nid:08X}")
        return 0

    if not args.names:
        parser.print_help()
        return 1

    for name in args.names:
        print(f"0x{compute_nid(name):08X}  {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
