#!/usr/bin/env python3
"""Build the generated GitHub Wiki tree from canonical DragonPWR docs.

The repository docs remain the source of truth. This script creates a flat,
GitHub-Wiki-friendly tree for the publishing workflow and rewrites local links
so mirrored pages keep working after they move out of docs/.
"""

from __future__ import annotations

import argparse
import os
import posixpath
import re
import shutil
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]

PAGE_MAP = {
    "README.md": "Project-Overview.md",
    "docs/INDEX.md": "Documentation-Index.md",
    "docs/INSTALLATION_AND_RECOVERY.md": "Installation-and-Recovery.md",
    "docs/ARCHITECTURE.md": "Architecture.md",
    "docs/API.md": "API.md",
    "docs/HARDWARE_ANALYSIS.md": "Hardware-Analysis.md",
    "docs/METERING_REVERSE_ENGINEERING.md": "Metering-Reverse-Engineering.md",
    "docs/BENCH_NOTES.md": "Bench-Notes.md",
    "docs/ROADMAP.md": "Roadmap.md",
    "analysis/README.md": "Stock-Firmware-Analysis-Tools.md",
}

STATIC_PAGES = ("wiki/Home.md", "wiki/_Sidebar.md")
LINK_RE = re.compile(r"(!?\[[^\]]*\])\(([^)]+)\)")


def split_anchor(target: str) -> tuple[str, str]:
    if "#" not in target:
        return target, ""
    path, anchor = target.split("#", 1)
    return path, "#" + anchor


def repo_url(path: str, *, image: bool, directory: bool) -> str:
    server = os.environ.get("GITHUB_SERVER_URL", "https://github.com").rstrip("/")
    repository = os.environ.get("GITHUB_REPOSITORY", "danielbrownjr/DragonPWR")
    ref = os.environ.get("GITHUB_SHA", "main")
    if image:
        if server == "https://github.com":
            return f"https://raw.githubusercontent.com/{repository}/{ref}/{path}"
        return f"{server}/{repository}/raw/{ref}/{path}"
    view = "tree" if directory else "blob"
    return f"{server}/{repository}/{view}/{ref}/{path}"


def rewrite_target(source_path: str, raw_target: str) -> str:
    target = raw_target.strip()
    if (
        not target
        or target.startswith(("#", "http://", "https://", "mailto:", "data:"))
        or target.startswith("<")
    ):
        return raw_target

    path_part, anchor = split_anchor(target)
    source_dir = str(PurePosixPath(source_path).parent)
    resolved = posixpath.normpath(posixpath.join(source_dir, path_part))

    mapped = PAGE_MAP.get(resolved)
    if mapped:
        return mapped + anchor

    image_prefix = "docs/images/"
    if resolved.startswith(image_prefix):
        return "images/" + resolved[len(image_prefix):] + anchor

    is_image = raw_target.startswith(("images/", "../images/")) or bool(
        re.search(r"\.(?:png|jpe?g|gif|webp|svg)$", resolved, re.I)
    )
    is_directory = path_part.endswith("/")
    return repo_url(resolved, image=is_image, directory=is_directory) + anchor


def rewrite_markdown(source_path: str, text: str) -> str:
    def replace(match: re.Match[str]) -> str:
        label, target = match.groups()
        return f"{label}({rewrite_target(source_path, target)})"

    return LINK_RE.sub(replace, text)


def build(output: Path) -> None:
    if output.exists():
        shutil.rmtree(output)
    output.mkdir(parents=True)

    missing = [p for p in [*PAGE_MAP, *STATIC_PAGES] if not (ROOT / p).exists()]
    if missing:
        raise SystemExit("missing wiki source files: " + ", ".join(missing))

    banner = (
        "> **Generated mirror.** The canonical source is in the DragonPWR "
        "repository. Edit the repository docs, not this wiki copy.\n\n"
    )

    for source, destination in PAGE_MAP.items():
        text = (ROOT / source).read_text(encoding="utf-8")
        text = rewrite_markdown(source, text)
        (output / destination).write_text(banner + text, encoding="utf-8")

    for source in STATIC_PAGES:
        destination = PurePosixPath(source).name
        text = (ROOT / source).read_text(encoding="utf-8")
        (output / destination).write_text(text, encoding="utf-8")

    images = ROOT / "docs" / "images"
    if images.exists():
        shutil.copytree(images, output / "images")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", default=".wiki-out")
    args = parser.parse_args()
    build(ROOT / args.output)


if __name__ == "__main__":
    main()
