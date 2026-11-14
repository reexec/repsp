#!/usr/bin/env python3
"""Fetch a pinned LGPL shared build of FFmpeg for repsp_codec_ffmpeg.

This script is invoked manually by developers; CMake never runs it automatically.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import platform
import shutil
import sys
import tarfile
import urllib.request
import zipfile

PINNED_TAG = "n8.1"

PINNED_BUILDS = {
    "win64": {
        "url": "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-11-13-12-43/ffmpeg-n8.1.3-35-gf1acb0a266-win64-lgpl-shared-8.1.zip",
        "sha256": "57cb10e4703b88edf55aa49e94dee19fd12f555f82ed3493a8cd70c847699996",
        "archive_type": "zip",
    },
    "linux64": {
        "url": "https://github.com/BtbN/FFmpeg-Builds/releases/download/autobuild-2026-11-13-12-43/ffmpeg-n8.1.3-35-gf1acb0a266-linux64-lgpl-shared-8.1.tar.xz",
        "sha256": "67b307fb2b07d2510a8d61cda583695560af1c76e355969e37133e66f98db7c4",
        "archive_type": "tar.xz",
    },
}


def detect_platform() -> str:
    sys_name = platform.system().lower()
    if sys_name == "windows":
        return "win64"
    if sys_name == "linux":
        return "linux64"
    raise RuntimeError(f"Unsupported host platform: {platform.system()}")


def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def verify_lgpl_license(root: pathlib.Path) -> None:
    candidates = [
        root / "LICENSE.txt",
        root / "COPYING.LGPLv2.1",
        root / "COPYING.LGPLv3",
    ]
    found_text = None
    for cand in candidates:
        if cand.is_file():
            found_text = cand.read_text(encoding="utf-8", errors="replace")
            break
    if found_text is None:
        raise RuntimeError(f"No LICENSE.txt or COPYING.LGPL* file found in {root}")
    upper = found_text.upper()
    if "GNU GENERAL PUBLIC LICENSE" in upper and "LESSER" not in upper:
        raise RuntimeError("Refusing non-LGPL (GPL) FFmpeg build")


def flatten_single_subdir(dest: pathlib.Path) -> None:
    entries = [p for p in dest.iterdir() if p.name != ".repsp_ffmpeg_manifest.json"]
    if len(entries) == 1 and entries[0].is_dir() and (entries[0] / "include").is_dir():
        inner = entries[0]
        for item in list(inner.iterdir()):
            target = dest / item.name
            if target.exists():
                if target.is_dir():
                    shutil.rmtree(target)
                else:
                    target.unlink()
            shutil.move(str(item), str(target))
        inner.rmdir()


def main() -> int:
    parser = argparse.ArgumentParser(description="Fetch pinned FFmpeg LGPL shared build")
    parser.add_argument("--platform", choices=["win64", "linux64"], default=None)
    parser.add_argument("--dest", type=pathlib.Path, default=None)
    parser.add_argument("--url", default=None, help="Override download URL (or file:// URL)")
    parser.add_argument("--sha256", default=None, help="Expected SHA-256 hex digest")
    parser.add_argument("--allow-unverified", action="store_true", help="Allow download when pinned sha256 is empty")
    args = parser.parse_args()

    repo_root = pathlib.Path(__file__).resolve().parent.parent
    plat = args.platform or detect_platform()
    dest = args.dest or (repo_root / "third_party" / "_ffmpeg")

    spec = PINNED_BUILDS[plat]
    url = args.url or spec["url"]
    expected_sha = args.sha256 if args.sha256 is not None else spec["sha256"]

    manifest_path = dest / ".repsp_ffmpeg_manifest.json"
    if manifest_path.is_file() and (dest / "include" / "libavcodec" / "avcodec.h").is_file():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            if manifest.get("url") == url and (not expected_sha or manifest.get("sha256") == expected_sha):
                print(f"FFmpeg already present at {dest} (skipping download)")
                return 0
        except Exception:
            pass

    if not expected_sha and not args.allow_unverified:
        print(
            "ERROR: No SHA-256 configured for this build. Pass --sha256 <hex> or --allow-unverified.",
            file=sys.stderr,
        )
        return 2

    dest.mkdir(parents=True, exist_ok=True)
    archive_name = "ffmpeg_download.zip" if url.endswith(".zip") else "ffmpeg_download.tar.xz"
    archive_path = dest / archive_name

    print("FFmpeg is a separate project licensed under the GNU LGPL (this pinned build: LGPL v3 or later).")
    print("It is not part of repsp and is not stored in the repository. A copy of the licence is")
    print("unpacked with the build (LICENSE.txt). Built by BtbN FFmpeg-Builds, one of the providers")
    print("linked from https://ffmpeg.org/download.html; the corresponding source is FFmpeg at the")
    print("revision named in the archive file name (https://git.ffmpeg.org/ffmpeg.git) with the build")
    print("scripts of https://github.com/BtbN/FFmpeg-Builds at the release tag in the URL.")
    print(f"Downloading {url} -> {archive_path}")
    urllib.request.urlretrieve(url, archive_path)

    actual_sha = sha256_file(archive_path)
    if expected_sha and actual_sha.lower() != expected_sha.lower():
        archive_path.unlink(missing_ok=True)
        print(f"ERROR: SHA-256 mismatch: expected {expected_sha}, got {actual_sha}", file=sys.stderr)
        return 1

    # Clear old extracted contents except the archive itself.
    for item in list(dest.iterdir()):
        if item == archive_path:
            continue
        if item.is_dir():
            shutil.rmtree(item)
        else:
            item.unlink()

    if archive_name.endswith(".zip"):
        with zipfile.ZipFile(archive_path, "r") as zf:
            zf.extractall(dest)
    else:
        with tarfile.open(archive_path, "r:xz") as tf:
            tf.extractall(dest)

    archive_path.unlink(missing_ok=True)
    flatten_single_subdir(dest)
    verify_lgpl_license(dest)

    manifest_path.write_text(
        json.dumps({"tag": PINNED_TAG, "platform": plat, "url": url, "sha256": actual_sha}, indent=2),
        encoding="utf-8",
    )
    print(f"FFmpeg LGPL build ready at {dest} (sha256={actual_sha})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
