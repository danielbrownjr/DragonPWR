#!/usr/bin/env python3
"""Fail closed if the DragonPWR dc_ui manifest and reproducible lock disagree."""
from pathlib import Path
import re


def ui_section(path):
    content = Path(path).read_text(encoding="utf-8")
    found = re.findall(r"(?ms)^  dc_ui:\n(.*?)(?=^  [A-Za-z_][\w./-]*:|\Z)", content)
    if len(found) != 1:
        raise SystemExit(f"{path}: expected exactly one dc_ui dependency block")
    return found[0]


def field(block, name, pattern):
    matches = re.findall(r"(?m)^ {4,6}" + re.escape(name) + r": (" + pattern + r")$", block)
    if len(matches) != 1:
        raise SystemExit(f"dc_ui: expected one valid {name} field")
    return matches[0]


manifest = ui_section("main/idf_component.yml")
locked = ui_section("dependencies.lock")
revision = field(manifest, "version", r"[0-9a-f]{40}")
locked_revision = field(locked, "version", r"[0-9a-f]{40}")
component_hash = field(locked, "component_hash", r"[0-9a-f]{64}")
expected_git = "https://github.com/justinh-rahb/dragon-core.git"
expected_path = "components/dc_ui"
for block in (manifest, locked):
    if field(block, "git", r"\S+") != expected_git:
        raise SystemExit("dc_ui: unexpected dependency repository")
    if field(block, "path", r"\S+") != expected_path:
        raise SystemExit("dc_ui: unexpected component path")
if revision != locked_revision:
    raise SystemExit(f"dc_ui manifest {revision} differs from lock {locked_revision}")
print(f"PASS: dc_ui manifest and lock pin {revision} (hash {component_hash})")
