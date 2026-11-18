#!/usr/bin/env python3
"""Build one board in an isolated source/config/dependency/build workspace."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", choices=("c2", "v2"))
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    idf = Path(os.environ["IDF_PATH"])
    version = subprocess.check_output(["git", "-C", str(idf), "describe", "--tags", "--exact-match"], text=True).strip()
    if version not in ("v5.3.5", "v6.1.1"):
        parser.error("activate the validated v5.3.5 or evaluation v6.1.1 environment")
    area = root / "build" / "native" / version / args.board
    source = area / "source"
    source.mkdir(parents=True, exist_ok=True)
    # Component Manager's Git cache is not safe for simultaneous checkouts.
    os.environ["IDF_COMPONENT_CACHE_PATH"] = str(area / "component-cache")
    # Copy only source-controlled files. Each cell owns its generated lock,
    # managed components and sdkconfig; the checkout's pins stay untouched.
    files = subprocess.check_output(["git", "-C", str(root), "ls-files", "-z"]).decode().split("\0")
    for name in filter(None, files):
        src = root / name
        if not src.is_file():
            continue
        dest = source / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dest)
    sha = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    (area / "source-head.txt").write_text(sha + "\n")
    target, defaults = (("esp32c2", "sdkconfig.defaults") if args.board == "c2"
                        else ("esp32c3", "sdkconfig.v2.defaults"))
    cmd = ["idf.py", "-C", str(source), "-B", str(area / "out"),
           "-D", f"IDF_TARGET={target}", "-D", f"SDKCONFIG={area / 'sdkconfig'}",
           "-D", f"SDKCONFIG_DEFAULTS={source / defaults}"]
    print("Source snapshot version is dev; source-head.txt records checkout HEAD.", flush=True)
    subprocess.run(cmd + ["reconfigure"], check=True)
    subprocess.run(["ninja", "-C", str(area / "out"), f"-j{args.jobs}"], check=True)
    subprocess.run(cmd + ["size"], check=True)


if __name__ == "__main__":
    main()
